#include "ESPIDFDsiPanel.h"

#if __has_include("MipiDsi.h")

#include <deki/LogSystem.h>

#include "blit/P4DmaCopy.h"

#if defined(ESP32)
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sdkconfig.h>
#include <soc/soc_caps.h>
#if SOC_MIPI_DSI_SUPPORTED
#include <driver/ledc.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#endif
#endif

#include <atomic>
#include <climits>

namespace DekiEsp32
{

#if defined(ESP32) && SOC_MIPI_DSI_SUPPORTED

namespace
{

/// The panel on DSI bus 0. The driver streams frame buffer after frame
/// buffer by DMA; at the end of each it starts the next from the buffer last
/// given to esp_lcd_panel_draw_bitmap and calls on_frame_buf_complete. So
/// after Show, the first such call is the end of the refresh that may still
/// have read the buffer shown before.
class ESPIDFDsiPanel : public DekiDisplay::IDsiPanel
{
public:
    ~ESPIDFDsiPanel() override
    {
        if (m_Panel)
        {
            esp_lcd_panel_del(m_Panel);
        }
        if (m_IO)
        {
            esp_lcd_panel_io_del(m_IO);
        }
        if (m_Bus)
        {
            esp_lcd_del_dsi_bus(m_Bus);
        }
        if (m_Refreshed)
        {
            vSemaphoreDelete(m_Refreshed);
        }
    }

    bool Open(const DekiDisplay::DsiPanelConfig& config)
    {
        m_Width = config.width;
        m_Height = config.height;

        esp_lcd_dsi_bus_config_t bus = {};
        bus.bus_id = 0;
        bus.num_data_lanes = config.dataLanes;
#if CONFIG_ESP32P4_SELECTS_REV_LESS_V3
        bus.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT_LEGACY;  // chips before revision 3
#else
        bus.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;
#endif
        bus.lane_bit_rate_mbps = static_cast<float>(config.laneMbps);
        esp_err_t err = esp_lcd_new_dsi_bus(&bus, &m_Bus);
        if (err != ESP_OK)
        {
            DEKI_LOG_ERROR("ESPIDFDsiPanel: the DSI bus did not start (%s); is the DSI PHY's supply (LDO 3, 2.5 V) on?",
                           esp_err_to_name(err));
            return false;
        }

        esp_lcd_dbi_io_config_t dbi = {};
        dbi.virtual_channel = 0;
        dbi.lcd_cmd_bits = 8;
        dbi.lcd_param_bits = 8;
        err = esp_lcd_new_panel_io_dbi(m_Bus, &dbi, &m_IO);
        if (err != ESP_OK)
        {
            DEKI_LOG_ERROR("ESPIDFDsiPanel: no command interface on the DSI bus (%s)", esp_err_to_name(err));
            return false;
        }

        esp_lcd_dpi_panel_config_t dpi = {};
        dpi.virtual_channel = 0;
        dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
        dpi.dpi_clock_freq_mhz = static_cast<float>(config.pixelClockHz) / 1000000.0f;
        dpi.in_color_format = LCD_COLOR_FMT_RGB565;
        dpi.out_color_format = LCD_COLOR_FMT_RGB565;
        dpi.num_fbs = config.frameBuffers < 1 ? 1 : (config.frameBuffers > 3 ? 3 : config.frameBuffers);
        dpi.video_timing.h_size = static_cast<uint32_t>(config.width);
        dpi.video_timing.v_size = static_cast<uint32_t>(config.height);
        dpi.video_timing.hsync_pulse_width = config.hsyncPulse;
        dpi.video_timing.hsync_back_porch = config.hBackPorch;
        dpi.video_timing.hsync_front_porch = config.hFrontPorch;
        dpi.video_timing.vsync_pulse_width = config.vsyncPulse;
        dpi.video_timing.vsync_back_porch = config.vBackPorch;
        dpi.video_timing.vsync_front_porch = config.vFrontPorch;
        err = esp_lcd_new_panel_dpi(m_Bus, &dpi, &m_Panel);
        if (err != ESP_OK)
        {
            DEKI_LOG_ERROR("ESPIDFDsiPanel: the video stream could not be set up (%s); %d frame buffer%s of %u KB "
                           "need PSRAM",
                           esp_err_to_name(err), static_cast<int>(dpi.num_fbs), dpi.num_fbs == 1 ? "" : "s",
                           static_cast<unsigned>(config.width * config.height * 2 / 1024));
            return false;
        }

        void* fbs[3] = { nullptr, nullptr, nullptr };
        err = dpi.num_fbs == 3   ? esp_lcd_dpi_panel_get_frame_buffer(m_Panel, 3, &fbs[0], &fbs[1], &fbs[2])
              : dpi.num_fbs == 2 ? esp_lcd_dpi_panel_get_frame_buffer(m_Panel, 2, &fbs[0], &fbs[1])
                                 : esp_lcd_dpi_panel_get_frame_buffer(m_Panel, 1, &fbs[0]);
        if (err != ESP_OK || !fbs[0])
        {
            DEKI_LOG_ERROR("ESPIDFDsiPanel: the driver gave no frame buffer (%s)", esp_err_to_name(err));
            return false;
        }
        m_Buffers[0] = static_cast<uint8_t*>(fbs[0]);
        m_Buffers[1] = static_cast<uint8_t*>(fbs[1]);
        m_Buffers[2] = static_cast<uint8_t*>(fbs[2]);
        m_BufferCount = dpi.num_fbs;

        m_Refreshed = xSemaphoreCreateBinary();
        if (!m_Refreshed)
        {
            return false;
        }
        esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
        callbacks.on_frame_buf_complete = &OnFrameBufferComplete;
        err = esp_lcd_dpi_panel_register_event_callbacks(m_Panel, &callbacks, this);
        if (err != ESP_OK)
        {
            DEKI_LOG_ERROR("ESPIDFDsiPanel: the refresh callback was refused (%s)", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    bool Command(uint8_t cmd, const uint8_t* params, size_t n) override
    {
        return esp_lcd_panel_io_tx_param(m_IO, cmd, n > 0 ? params : nullptr, n) == ESP_OK;
    }

    bool Start() override
    {
        const esp_err_t err = esp_lcd_panel_init(m_Panel);
        if (err != ESP_OK)
        {
            DEKI_LOG_ERROR("ESPIDFDsiPanel: the video stream did not start (%s)", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    int32_t GetFrameBufferCount() const override { return m_BufferCount; }

    uint8_t* GetFrameBuffer(int32_t index) const override
    {
        return index >= 0 && index < m_BufferCount ? m_Buffers[index] : nullptr;
    }

    bool Show(int32_t index) override
    {
        if (index < 0 || index >= m_BufferCount)
        {
            return false;
        }
        // The driver sees the buffer is its own: it writes the CPU's cache
        // back to memory and streams this buffer from the next refresh. A
        // refresh may end while it does, and the next start from either
        // buffer: while m_InShow is set, the callback counts both as sent.
        m_Previous.store(m_Queued.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_InShow.store(true, std::memory_order_release);
        m_Queued.store(index, std::memory_order_release);
        const esp_err_t err = esp_lcd_panel_draw_bitmap(m_Panel, 0, 0, m_Width, m_Height, m_Buffers[index]);
        m_InShow.store(false, std::memory_order_release);
        return err == ESP_OK;
    }

    uint32_t GetBusyMask() const override
    {
        return m_Sending.load(std::memory_order_acquire) | (1u << m_Queued.load(std::memory_order_acquire));
    }

    bool WaitRefresh(uint32_t timeoutMs) override
    {
        // A refresh that ended before this call is not the one waited for.
        xSemaphoreTake(m_Refreshed, 0);
        return xSemaphoreTake(m_Refreshed, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }

    bool GetFramesShown(uint64_t* frames, uint64_t* lastUs) const override
    {
        portENTER_CRITICAL(&m_CountLock);
        const uint64_t shown = m_FramesShown;
        const uint64_t at = m_LastShownUs;
        portEXIT_CRITICAL(&m_CountLock);
        if (frames)
        {
            *frames = shown;
        }
        if (lastUs)
        {
            *lastUs = at;
        }
        return true;
    }

    // The backlight on LEDC timer 0, channel 0, as Elecrow's own firmware
    // drives its panels' backlights: low-speed mode, 11 bits of duty.
    bool StartBacklightPwm(int32_t pin, uint32_t hz, bool invert) override
    {
        ledc_timer_config_t timer = {};
        timer.speed_mode = LEDC_LOW_SPEED_MODE;
        timer.duty_resolution = kBacklightBits;
        timer.timer_num = LEDC_TIMER_0;
        timer.freq_hz = hz;
        timer.clk_cfg = LEDC_AUTO_CLK;
        esp_err_t err = ledc_timer_config(&timer);
        if (err != ESP_OK)
        {
            DEKI_LOG_ERROR("ESPIDFDsiPanel: no PWM at %u Hz for the backlight (%s); it is switched on and off instead",
                           static_cast<unsigned>(hz), esp_err_to_name(err));
            return false;
        }
        ledc_channel_config_t channel = {};
        channel.gpio_num = pin;
        channel.speed_mode = LEDC_LOW_SPEED_MODE;
        channel.channel = LEDC_CHANNEL_0;
        channel.timer_sel = LEDC_TIMER_0;
        channel.duty = 0;
        channel.flags.output_invert = invert ? 1 : 0;
        err = ledc_channel_config(&channel);
        if (err != ESP_OK)
        {
            DEKI_LOG_ERROR("ESPIDFDsiPanel: the backlight's PWM channel did not start (%s)", esp_err_to_name(err));
            return false;
        }
        m_BacklightPwm = true;
        return true;
    }

    bool SetBacklightLevel(float level) override
    {
        if (!m_BacklightPwm)
        {
            return false;
        }
        const float clamped = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
        // Full duty is 2^bits: the pin then stays high.
        const uint32_t duty = static_cast<uint32_t>(clamped * static_cast<float>(1u << kBacklightBits) + 0.5f);
        return ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty) == ESP_OK &&
               ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == ESP_OK;
    }

    bool StartRowsCopy(int32_t index, int32_t y, int32_t rows, const uint8_t* src, int32_t srcStridePixels) override
    {
        if (index < 0 || index >= m_BufferCount || y < 0 || rows <= 0 || y + rows > m_Height)
        {
            return false;
        }
        const size_t rowBytes = static_cast<size_t>(m_Width) * 2;
        // Left running: whole rows of the frame buffer, on whole cache lines.
        return m_Rows.Copy(src, static_cast<size_t>(srcStridePixels) * 2, m_Buffers[index] + static_cast<size_t>(y) * rowBytes,
                           rowBytes, rowBytes, rows, true) != QuadBlit::RectCopyResult::Refused;
    }

    void WaitRowsCopy() override { m_Rows.Wait(INT32_MAX); }

    bool CopyRowsBetween(int32_t from, int32_t to, int32_t y, int32_t rows) override
    {
        if (from < 0 || from >= m_BufferCount || to < 0 || to >= m_BufferCount || y < 0 || rows <= 0 ||
            y + rows > m_Height)
        {
            return false;
        }
        const size_t rowBytes = static_cast<size_t>(m_Width) * 2;
        const size_t at = static_cast<size_t>(y) * rowBytes;
        if (m_Rows.Copy(m_Buffers[from] + at, rowBytes, m_Buffers[to] + at, rowBytes, rowBytes, rows, true) ==
            QuadBlit::RectCopyResult::Refused)
        {
            return false;
        }
        m_Rows.Wait(INT32_MAX);
        return true;
    }

private:
    static constexpr ledc_timer_bit_t kBacklightBits = LEDC_TIMER_11_BIT;

    // Strips and kept rows into the frame buffers, by DMA where the chip has
    // it (the P4), on a channel of their own.
    Blit::DmaEngine m_Rows{ "frame rows" };

    static bool IRAM_ATTR OnFrameBufferComplete(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void* ctx)
    {
        // A refresh ended, and the next began from the buffer shown last.
        auto* self = static_cast<ESPIDFDsiPanel*>(ctx);
        const int32_t queued = self->m_Queued.load(std::memory_order_acquire);
        uint32_t sending = 1u << queued;
        if (self->m_InShow.load(std::memory_order_acquire))
        {
            sending |= 1u << self->m_Previous.load(std::memory_order_relaxed);
        }
        self->m_Sending.store(sending, std::memory_order_release);
        if (queued != self->m_LastSent)
        {
            self->m_LastSent = queued;
            const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
            portENTER_CRITICAL_ISR(&self->m_CountLock);
            ++self->m_FramesShown;
            self->m_LastShownUs = now;
            portEXIT_CRITICAL_ISR(&self->m_CountLock);
        }
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(self->m_Refreshed, &woken);
        return woken == pdTRUE;
    }

    esp_lcd_dsi_bus_handle_t m_Bus = nullptr;
    esp_lcd_panel_io_handle_t m_IO = nullptr;
    esp_lcd_panel_handle_t m_Panel = nullptr;
    int32_t m_Width = 0;
    int32_t m_Height = 0;
    uint8_t* m_Buffers[3] = { nullptr, nullptr, nullptr };
    int32_t m_BufferCount = 0;
    bool m_BacklightPwm = false;
    SemaphoreHandle_t m_Refreshed = nullptr;  // given at the end of every refresh
    // The buffer shown last (the next refresh sends it), the one before it
    // while a Show is under way, the ones being sent now (a bit each; buffer
    // 0 at start), and the last one counted as having gone up.
    std::atomic<int32_t> m_Queued{ 0 };
    std::atomic<int32_t> m_Previous{ 0 };
    std::atomic<bool> m_InShow{ false };
    std::atomic<uint32_t> m_Sending{ 1u };
    int32_t m_LastSent = 0;  // the interrupt's own
    // 64-bit values, which a 32-bit core cannot store at once: read and
    // written under a lock, the interrupt's side included.
    mutable portMUX_TYPE m_CountLock = portMUX_INITIALIZER_UNLOCKED;
    uint64_t m_FramesShown = 0;
    uint64_t m_LastShownUs = 0;
};

}  // namespace

std::unique_ptr<DekiDisplay::IDsiPanel> CreateESPIDFDsiPanel(const DekiDisplay::DsiPanelConfig& config)
{
    auto panel = std::make_unique<ESPIDFDsiPanel>();
    if (!panel->Open(config))
    {
        return nullptr;
    }
    return panel;
}

#else

std::unique_ptr<DekiDisplay::IDsiPanel> CreateESPIDFDsiPanel(const DekiDisplay::DsiPanelConfig&)
{
    DEKI_LOG_ERROR("ESPIDFDsiPanel: this chip has no MIPI-DSI (the ESP32-P4 has)");
    return nullptr;
}

#endif

}  // namespace DekiEsp32

#endif  // __has_include("MipiDsi.h")
