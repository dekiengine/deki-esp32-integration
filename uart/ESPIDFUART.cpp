#include "ESPIDFUART.h"
#include "DekiLogSystem.h"

#if defined(ESP32)
#include "driver/uart.h"
#endif

void ESPIDFUART::Configure(const ModuleConfig& config)
{
    m_PinTX = config.GetPin("TX", -1);
    m_PinRX = config.GetPin("RX", -1);
    m_Port  = config.GetInt("uart_port", 1);
    m_Baud  = (uint32_t)config.GetInt("baud", 9600);
    m_RxBufSize = (size_t)config.GetInt("rx_buf_size", 1024);
}

bool ESPIDFUART::Initialize()
{
#if defined(ESP32)
    if (m_PinRX < 0)
    {
        m_LastError = "ESPIDFUART: RX pin not configured";
        m_State = ModuleState::Error;
        return false;
    }

    uart_config_t cfg = {};
    cfg.baud_rate  = (int)m_Baud;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    if (uart_param_config((uart_port_t)m_Port, &cfg) != ESP_OK ||
        uart_set_pin((uart_port_t)m_Port, m_PinTX, m_PinRX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install((uart_port_t)m_Port, (int)m_RxBufSize, 0, 0, nullptr, 0) != ESP_OK)
    {
        m_LastError = "ESPIDFUART: uart driver install failed";
        m_State = ModuleState::Error;
        return false;
    }

    m_State = ModuleState::Running;
    return true;
#else
    m_LastError = "ESPIDFUART: hardware path only built for ESP32";
    m_State = ModuleState::Error;
    return false;
#endif
}

void ESPIDFUART::Shutdown()
{
#if defined(ESP32)
    uart_driver_delete((uart_port_t)m_Port);
#endif
    m_State = ModuleState::Uninitialized;
}

int ESPIDFUART::Read(uint8_t* dst, size_t maxLen, uint32_t timeoutMs)
{
#if defined(ESP32)
    if (!dst || maxLen == 0) return 0;
    return uart_read_bytes((uart_port_t)m_Port, dst, maxLen, pdMS_TO_TICKS(timeoutMs));
#else
    (void)dst; (void)maxLen; (void)timeoutMs; return 0;
#endif
}

int ESPIDFUART::Write(const uint8_t* src, size_t len)
{
#if defined(ESP32)
    if (!src || len == 0) return 0;
    return uart_write_bytes((uart_port_t)m_Port, (const char*)src, len);
#else
    (void)src; (void)len; return 0;
#endif
}
