#include "ESPIDFI2C.h"
#include "DekiLogSystem.h"

#if defined(ESP32)
#include "driver/i2c.h"
#endif

void ESPIDFI2C::Configure(const ModuleConfig& config)
{
    m_PinSDA = config.GetPin("SDA", -1);
    m_PinSCL = config.GetPin("SCL", -1);
    m_Port   = config.GetInt("i2c_port", 0);
    m_FreqHz = (uint32_t)config.GetInt("i2c_hz", 400000);
}

bool ESPIDFI2C::Initialize()
{
#if defined(ESP32)
    if (m_PinSDA < 0 || m_PinSCL < 0)
    {
        m_LastError = "ESPIDFI2C: SDA/SCL pins not configured";
        m_State = ModuleState::Error;
        return false;
    }

    i2c_config_t conf = {};
    conf.mode             = I2C_MODE_MASTER;
    conf.sda_io_num       = (gpio_num_t)m_PinSDA;
    conf.scl_io_num       = (gpio_num_t)m_PinSCL;
    conf.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = m_FreqHz;

    if (i2c_param_config((i2c_port_t)m_Port, &conf) != ESP_OK ||
        i2c_driver_install((i2c_port_t)m_Port, conf.mode, 0, 0, 0) != ESP_OK)
    {
        m_LastError = "ESPIDFI2C: i2c driver install failed";
        m_State = ModuleState::Error;
        return false;
    }

    m_State = ModuleState::Initialized;
    return true;
#else
    m_LastError = "ESPIDFI2C: hardware path only built for ESP32";
    m_State = ModuleState::Error;
    return false;
#endif
}

void ESPIDFI2C::Shutdown()
{
#if defined(ESP32)
    i2c_driver_delete((i2c_port_t)m_Port);
#endif
    m_State = ModuleState::Uninitialized;
}

bool ESPIDFI2C::Probe(uint8_t addr)
{
#if defined(ESP32)
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin((i2c_port_t)m_Port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
#else
    (void)addr; return false;
#endif
}

bool ESPIDFI2C::Read(uint8_t addr, uint8_t reg, uint8_t* dst, size_t len)
{
#if defined(ESP32)
    if (!dst || len == 0) return false;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, dst, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, dst + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin((i2c_port_t)m_Port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
#else
    (void)addr; (void)reg; (void)dst; (void)len; return false;
#endif
}

bool ESPIDFI2C::Write(uint8_t addr, uint8_t reg, const uint8_t* src, size_t len)
{
#if defined(ESP32)
    if (!src || len == 0) return false;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, (uint8_t*)src, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin((i2c_port_t)m_Port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
#else
    (void)addr; (void)reg; (void)src; (void)len; return false;
#endif
}
