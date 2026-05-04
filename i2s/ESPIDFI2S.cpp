#include "ESPIDFI2S.h"
#include "DekiLogSystem.h"

#if defined(ESP32)
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#endif

ESPIDFI2S::~ESPIDFI2S()
{
    Shutdown();
}

void ESPIDFI2S::Configure(const ModuleConfig& config)
{
    m_PinBCLK       = config.GetPin("BCLK", -1);
    m_PinLRCLK      = config.GetPin("LRCLK", -1);
    m_PinDOUT       = config.GetPin("DOUT", -1);
    m_Port          = config.GetInt("i2s_port", 0);
    m_SampleRate    = config.GetInt("sample_rate", 16000);
    m_BitsPerSample = config.GetInt("bits_per_sample", 16);
    m_Channels      = config.GetInt("channels", 1);
}

bool ESPIDFI2S::Initialize()
{
#if defined(ESP32)
    if (m_PinBCLK < 0 || m_PinLRCLK < 0 || m_PinDOUT < 0)
    {
        m_LastError = "ESPIDFI2S: BCLK/LRCLK/DOUT pins not configured";
        m_State = ModuleState::Error;
        return false;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)m_Port, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &m_TxHandle, nullptr) != ESP_OK)
    {
        m_LastError = "ESPIDFI2S: i2s_new_channel failed";
        m_State = ModuleState::Error;
        return false;
    }

    i2s_data_bit_width_t bits =
        (m_BitsPerSample == 24) ? I2S_DATA_BIT_WIDTH_24BIT :
        (m_BitsPerSample == 32) ? I2S_DATA_BIT_WIDTH_32BIT :
                                  I2S_DATA_BIT_WIDTH_16BIT;
    i2s_slot_mode_t slot =
        (m_Channels == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)m_SampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, slot),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)m_PinBCLK,
            .ws   = (gpio_num_t)m_PinLRCLK,
            .dout = (gpio_num_t)m_PinDOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    if (i2s_channel_init_std_mode(m_TxHandle, &std_cfg) != ESP_OK)
    {
        m_LastError = "ESPIDFI2S: i2s_channel_init_std_mode failed";
        i2s_del_channel(m_TxHandle);
        m_TxHandle = nullptr;
        m_State = ModuleState::Error;
        return false;
    }

    m_State = ModuleState::Initialized;
    return true;
#else
    m_LastError = "ESPIDFI2S: hardware path only built for ESP32";
    m_State = ModuleState::Error;
    return false;
#endif
}

void ESPIDFI2S::Shutdown()
{
#if defined(ESP32)
    if (m_TxHandle)
    {
        if (m_Running) i2s_channel_disable(m_TxHandle);
        i2s_del_channel(m_TxHandle);
        m_TxHandle = nullptr;
    }
#endif
    m_Running = false;
    m_State = ModuleState::Uninitialized;
}

bool ESPIDFI2S::Start()
{
#if defined(ESP32)
    if (!m_TxHandle) return false;
    if (m_Running) return true;
    if (i2s_channel_enable(m_TxHandle) != ESP_OK)
    {
        m_LastError = "ESPIDFI2S: i2s_channel_enable failed";
        return false;
    }
    m_Running = true;
    m_State = ModuleState::Running;
    return true;
#else
    return false;
#endif
}

bool ESPIDFI2S::Stop()
{
#if defined(ESP32)
    if (!m_TxHandle || !m_Running) return true;
    if (i2s_channel_disable(m_TxHandle) != ESP_OK)
    {
        m_LastError = "ESPIDFI2S: i2s_channel_disable failed";
        return false;
    }
    m_Running = false;
    m_State = ModuleState::Initialized;
    return true;
#else
    return false;
#endif
}

int ESPIDFI2S::Write(const void* data, size_t bytes, uint32_t timeoutMs)
{
#if defined(ESP32)
    if (!m_TxHandle || !data || bytes == 0) return 0;
    size_t written = 0;
    esp_err_t err = i2s_channel_write(m_TxHandle, data, bytes, &written, pdMS_TO_TICKS(timeoutMs));
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) return 0;
    return (int)written;
#else
    (void)data; (void)bytes; (void)timeoutMs; return 0;
#endif
}
