#pragma once

#include "providers/IDekiI2C.h"
#include "ModuleConfig.h"
#include <string>

class ESPIDFI2C : public IDekiI2C
{
public:
    ESPIDFI2C() = default;
    ~ESPIDFI2C() override = default;

    const char* GetModuleId() const override   { return "i2c"; }
    const char* GetModuleName() const override { return "I2C (ESP-IDF)"; }
    void        Configure(const ModuleConfig& config) override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    ModuleState GetState() const override      { return m_State; }
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

    ModuleState m_State = ModuleState::Uninitialized;
    std::string m_LastError;
};
