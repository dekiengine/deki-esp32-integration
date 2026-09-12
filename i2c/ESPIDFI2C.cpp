#include "ESPIDFI2C.h"
#include <deki/LogSystem.h>
#include <vector>

#if defined(ESP32)
// The new ("driver_ng") I2C driver, not the legacy driver/i2c.h.
//
// ESP-IDF refuses to have both in one application and aborts during startup
// with "CONFLICT! driver_ng is not allowed to be used with this old driver".
// LovyanGFX uses the new one, so any board with both a display and this
// package used to abort before reaching app_main. The legacy driver is
// deprecated anyway.
//
// The shape differs: the old driver addressed a port and carried the target
// address in each transaction, the new one hands out a handle per device. So
// a small cache below turns an address back into a handle, which keeps
// IDekiI2C's address-per-call interface intact.
#include "driver/i2c_master.h"
#endif

void ESPIDFI2C::Configure(const Deki::PackageConfig& config)
{
    m_PinSDA = config.GetPin("SDA", -1);
    m_PinSCL = config.GetPin("SCL", -1);
    m_Port   = config.GetInt("i2cPort", 0);
    m_FreqHz = (uint32_t)config.GetInt("i2cHz", 400000);
}

#if defined(ESP32)
// A device handle for `addr`, created on first use and kept for the life of
// the bus. Devices are few and long-lived (a touch panel, an IMU, an RTC), so
// a linear scan over a small vector beats a map here.
void* ESPIDFI2C::DeviceFor(uint8_t addr)
{
    for (const auto& d : m_Devices)
        if (d.addr == addr)
            return d.handle;

    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address  = addr;
    cfg.scl_speed_hz    = m_FreqHz;

    i2c_master_dev_handle_t handle = nullptr;
    if (i2c_master_bus_add_device((i2c_master_bus_handle_t)m_Bus, &cfg, &handle) != ESP_OK)
    {
        m_LastError = "ESPIDFI2C: could not add device at that address";
        return nullptr;
    }

    m_Devices.push_back({ addr, (void*)handle });
    return handle;
}
#endif

bool ESPIDFI2C::Initialize()
{
#if defined(ESP32)
    if (m_PinSDA < 0 || m_PinSCL < 0)
    {
        m_LastError = "ESPIDFI2C: SDA/SCL pins not configured";
        m_State = Deki::PackageState::Error;
        return false;
    }

    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port                     = m_Port;
    cfg.sda_io_num                   = (gpio_num_t)m_PinSDA;
    cfg.scl_io_num                   = (gpio_num_t)m_PinSCL;
    cfg.clk_source                   = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt            = 7;
    cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = nullptr;
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK)
    {
        m_LastError = "ESPIDFI2C: i2c master bus creation failed";
        m_State = Deki::PackageState::Error;
        return false;
    }

    m_Bus = bus;
    m_State = Deki::PackageState::Initialized;
    return true;
#else
    m_LastError = "ESPIDFI2C: hardware path only built for ESP32";
    m_State = Deki::PackageState::Error;
    return false;
#endif
}

void ESPIDFI2C::Shutdown()
{
#if defined(ESP32)
    // Devices before the bus: deleting the bus under live device handles
    // leaves them dangling.
    for (const auto& d : m_Devices)
        i2c_master_bus_rm_device((i2c_master_dev_handle_t)d.handle);
    m_Devices.clear();

    if (m_Bus)
    {
        i2c_del_master_bus((i2c_master_bus_handle_t)m_Bus);
        m_Bus = nullptr;
    }
#endif
    m_State = Deki::PackageState::Uninitialized;
}

bool ESPIDFI2C::Probe(uint8_t addr)
{
#if defined(ESP32)
    if (!m_Bus) return false;
    // The bus probes directly, so a device that is not there costs no handle.
    return i2c_master_probe((i2c_master_bus_handle_t)m_Bus, addr, 50) == ESP_OK;
#else
    (void)addr; return false;
#endif
}

bool ESPIDFI2C::Read(uint8_t addr, uint8_t reg, uint8_t* dst, size_t len)
{
#if defined(ESP32)
    if (!dst || len == 0 || !m_Bus) return false;
    auto dev = (i2c_master_dev_handle_t)DeviceFor(addr);
    if (!dev) return false;

    // Write the register then read, as one transaction with a repeated start:
    // the driver does what the old cmd link was hand-built to do.
    return i2c_master_transmit_receive(dev, &reg, 1, dst, len, 50) == ESP_OK;
#else
    (void)addr; (void)reg; (void)dst; (void)len; return false;
#endif
}

bool ESPIDFI2C::Write(uint8_t addr, uint8_t reg, const uint8_t* src, size_t len)
{
#if defined(ESP32)
    if (!src || len == 0 || !m_Bus) return false;
    auto dev = (i2c_master_dev_handle_t)DeviceFor(addr);
    if (!dev) return false;

    // One transmission carrying the register and the payload; the new driver
    // has no scatter form, so they are joined first. Small by construction:
    // these are device registers, not bulk transfers.
    std::vector<uint8_t> buffer;
    buffer.reserve(len + 1);
    buffer.push_back(reg);
    buffer.insert(buffer.end(), src, src + len);

    return i2c_master_transmit(dev, buffer.data(), buffer.size(), 50) == ESP_OK;
#else
    (void)addr; (void)reg; (void)src; (void)len; return false;
#endif
}
