#include "ESPIDFParallelBus.h"

#if __has_include("ParallelBus.h")

#include <deki/LogSystem.h>

#if defined(ESP32)
#include <driver/gpio.h>
#include <esp_lcd_io_i80.h>
#include <esp_memory_utils.h>
#include <esp_lcd_panel_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <soc/soc_caps.h>
#endif

namespace DekiEsp32
{

#if defined(ESP32) && SOC_LCD_I80_SUPPORTED

namespace
{

// The driver copies parameter bytes into a buffer of 32 (its
// LCD_I80_IO_FORMAT_BUF_SIZE): longer runs go in pieces.
constexpr size_t kParamBufferBytes = 32;
// DMA alignment: internal memory on 4 bytes, external on the burst size.
constexpr size_t kInternalAlign = 4;
constexpr size_t kExternalAlign = 64;

/// One panel on its own i80 bus. Pixel transfers are queued in the driver,
/// each signalling a semaphore as it finishes, so Wait counts them back.
class ESPIDFParallelBus : public DekiDisplay::IParallelBus
{
public:
    ~ESPIDFParallelBus() override
    {
        if (m_IO)
        {
            Wait(0);
            esp_lcd_panel_io_del(m_IO);
        }
        if (m_Bus)
        {
            esp_lcd_del_i80_bus(m_Bus);
        }
        if (m_Done)
        {
            vSemaphoreDelete(m_Done);
        }
    }

    bool Start(const DekiDisplay::ParallelBusConfig& config)
    {
        m_Width = config.width == 16 ? 16 : 8;
        m_MaxTransferBytes = config.maxTransferBytes;
        m_Done = xSemaphoreCreateCounting(config.queueDepth, 0);
        if (!m_Done)
        {
            DEKI_LOG_ERROR("ESPIDFParallelBus: no memory for its semaphore");
            return false;
        }
        // Read is not used: RD is held high so the panel never drives the bus.
        if (config.rdPin >= 0)
        {
            gpio_config_t rd = {};
            rd.pin_bit_mask = 1ULL << config.rdPin;
            rd.mode = GPIO_MODE_OUTPUT;
            gpio_config(&rd);
            gpio_set_level(static_cast<gpio_num_t>(config.rdPin), 1);
        }

        esp_lcd_i80_bus_config_t bus = {};
        bus.dc_gpio_num = static_cast<gpio_num_t>(config.dcPin);
        bus.wr_gpio_num = static_cast<gpio_num_t>(config.wrPin);
        bus.clk_src = LCD_CLK_SRC_DEFAULT;
        for (int i = 0; i < ESP_LCD_I80_BUS_WIDTH_MAX; ++i)
        {
            bus.data_gpio_nums[i] = static_cast<gpio_num_t>(i < m_Width ? config.dataPins[i] : -1);
        }
        bus.bus_width = m_Width;
        bus.max_transfer_bytes = m_MaxTransferBytes;
        bus.dma_burst_size = kExternalAlign;
        esp_err_t err = esp_lcd_new_i80_bus(&bus, &m_Bus);
        if (err != ESP_OK)
        {
            DEKI_LOG_ERROR("ESPIDFParallelBus: esp_lcd_new_i80_bus: %s", esp_err_to_name(err));
            return false;
        }

        // On 16 lines a command and each parameter take one 16-bit cycle,
        // the byte on D0-D7, as LovyanGFX's Bus_Parallel16 sends them.
        esp_lcd_panel_io_i80_config_t io = {};
        io.cs_gpio_num = static_cast<gpio_num_t>(config.csPin);
        io.pclk_hz = config.clockHz;
        io.trans_queue_depth = config.queueDepth;
        io.on_color_trans_done = &ESPIDFParallelBus::OnDone;
        io.user_ctx = this;
        io.lcd_cmd_bits = m_Width;
        io.lcd_param_bits = m_Width;
        io.dc_levels.dc_idle_level = 0;
        io.dc_levels.dc_cmd_level = 0;
        io.dc_levels.dc_dummy_level = 0;
        io.dc_levels.dc_data_level = 1;
        err = esp_lcd_new_panel_io_i80(m_Bus, &io, &m_IO);
        if (err != ESP_OK)
        {
            DEKI_LOG_ERROR("ESPIDFParallelBus: esp_lcd_new_panel_io_i80: %s", esp_err_to_name(err));
            return false;
        }
        DEKI_LOG_INFO("ESPIDFParallelBus: %d-bit bus up (WR %d, DC %d, CS %d, D0 %d) at %u Hz", m_Width,
                      (int)config.wrPin, (int)config.dcPin, (int)config.csPin, (int)config.dataPins[0],
                      (unsigned)config.clockHz);
        return true;
    }

    bool Command(uint8_t cmd, const uint8_t* params, size_t n) override
    {
        // The driver waits for the queued pixels itself, but counts them back
        // only through OnDone: drain the count so Wait stays right.
        Wait(0);
        return Param(cmd, params, n);
    }

    bool QueuePixels(int32_t cmd, const void* data, size_t bytes) override
    {
        const auto* p = static_cast<const uint8_t*>(data);
        // DMA takes buffers on its alignment; the odd bytes at either end go
        // out as parameter bytes, which are pixels on the wire just the same.
        const size_t align = esp_ptr_external_ram(p) ? kExternalAlign : kInternalAlign;
        const size_t head = (align - (reinterpret_cast<uintptr_t>(p) & (align - 1))) & (align - 1);
        if (head >= bytes)
        {
            Wait(0);
            return Param(cmd, p, bytes);
        }
        bool ok = true;
        if (head > 0)
        {
            Wait(0);
            ok = Param(cmd, p, head);
            cmd = -1;
            p += head;
            bytes -= head;
        }
        const size_t body = bytes & ~(align - 1);
        const size_t tail = bytes - body;
        if (ok && body > 0)
        {
            ok = esp_lcd_panel_io_tx_color(m_IO, static_cast<int>(cmd), p, body) == ESP_OK;
            if (ok)
            {
                ++m_InFlight;
            }
            cmd = -1;
        }
        if (ok && tail > 0)
        {
            Wait(0);
            ok = Param(cmd, p + body, tail);
        }
        return ok;
    }

    bool Wait(size_t leaveInFlight) override
    {
        while (m_InFlight > leaveInFlight)
        {
            if (xSemaphoreTake(m_Done, pdMS_TO_TICKS(1000)) != pdTRUE)
            {
                DEKI_LOG_ERROR("ESPIDFParallelBus: a pixel transfer did not finish in a second");
                m_InFlight = 0;
                return false;
            }
            --m_InFlight;
        }
        return true;
    }

    size_t GetMaxTransferBytes() const override { return m_MaxTransferBytes; }
    uint8_t GetWidth() const override { return static_cast<uint8_t>(m_Width); }

private:
    static bool OnDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* ctx)
    {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(static_cast<ESPIDFParallelBus*>(ctx)->m_Done, &woken);
        return woken == pdTRUE;
    }

    // A command (or none, -1) and bytes after it, sent before this returns,
    // in pieces the driver's buffer holds; the pieces after the first carry
    // no command, so the bytes go on as one run. On 16 lines each byte is
    // widened to a cycle of its own.
    bool Param(int32_t cmd, const uint8_t* bytes, size_t n)
    {
        const size_t piece = m_Width == 8 ? kParamBufferBytes : kParamBufferBytes / 2;
        bool ok = true;
        do
        {
            const size_t k = n < piece ? n : piece;
            if (m_Width == 8)
            {
                ok = esp_lcd_panel_io_tx_param(m_IO, static_cast<int>(cmd), k ? bytes : nullptr, k) == ESP_OK;
            }
            else
            {
                uint16_t wide[kParamBufferBytes / 2];
                for (size_t i = 0; i < k; ++i)
                {
                    wide[i] = bytes[i];
                }
                ok = esp_lcd_panel_io_tx_param(m_IO, static_cast<int>(cmd), k ? wide : nullptr, k * 2) == ESP_OK;
            }
            cmd = -1;
            bytes += k;
            n -= k;
        } while (ok && n > 0);
        return ok;
    }

    esp_lcd_i80_bus_handle_t m_Bus = nullptr;
    esp_lcd_panel_io_handle_t m_IO = nullptr;
    SemaphoreHandle_t m_Done = nullptr;
    int m_Width = 8;
    size_t m_MaxTransferBytes = 0;
    size_t m_InFlight = 0;
};

}  // namespace

std::unique_ptr<DekiDisplay::IParallelBus> CreateESPIDFParallelBus(const DekiDisplay::ParallelBusConfig& config)
{
    auto bus = std::make_unique<ESPIDFParallelBus>();
    if (!bus->Start(config))
    {
        return nullptr;
    }
    return bus;
}

#else

std::unique_ptr<DekiDisplay::IParallelBus> CreateESPIDFParallelBus(const DekiDisplay::ParallelBusConfig&)
{
    DEKI_LOG_ERROR("ESPIDFParallelBus: this chip has no parallel LCD peripheral");
    return nullptr;
}

#endif

}  // namespace DekiEsp32

#endif  // __has_include("ParallelBus.h")
