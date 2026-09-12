#pragma once

#include "IDekiI2C.h"  // from deki-i2c
#include <deki/PackageConfig.h>
#include <string>
#include <vector>

class ESPIDFI2C : public IDekiI2C
{
public:
    ESPIDFI2C() = default;
    ~ESPIDFI2C() override = default;

    const char* GetPackageId() const override   { return "i2c"; }
    const char* GetPackageName() const override { return "I2C (ESP-IDF)"; }
    void        Configure(const Deki::PackageConfig& config) override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    Deki::PackageState GetState() const override      { return m_State; }
    const char* GetLastError() const override  { return m_LastError.c_str(); }

    int  GetPort() const override         { return m_Port; }
    int  GetSdaPin() const override       { return m_PinSDA; }
    int  GetSclPin() const override       { return m_PinSCL; }
    int  GetFrequencyHz() const override  { return (int)m_FreqHz; }
    bool Probe(uint8_t addr) override;
    bool Read (uint8_t addr, uint8_t reg, uint8_t* dst, size_t len) override;
    bool Write(uint8_t addr, uint8_t reg, const uint8_t* src, size_t len) override;

private:
    int         m_PinSDA = -1;
    int         m_PinSCL = -1;
    int         m_Port   = 0;
    uint32_t    m_FreqHz = 400000;

#if defined(ESP32)
    // The new I2C driver hands out a handle per device rather than taking an
    // address per transaction, so addresses are mapped back to handles here.
    // Typed void* in the header to keep driver/i2c_master.h out of it, which
    // the desktop build has no business including.
    struct Device
    {
        uint8_t addr;
        void*   handle;
    };
    void* m_Bus = nullptr;
    std::vector<Device> m_Devices;

    void* DeviceFor(uint8_t addr);
#endif

    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    std::string m_LastError;
};
