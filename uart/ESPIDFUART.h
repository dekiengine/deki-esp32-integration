#pragma once

#include "providers/IDekiUART.h"
#include "ModuleConfig.h"
#include <string>

class ESPIDFUART : public IDekiUART
{
public:
    ESPIDFUART() = default;
    ~ESPIDFUART() override = default;

    const char* GetModuleId() const override   { return "uart"; }
    const char* GetModuleName() const override { return "UART (ESP-IDF)"; }
    void        Configure(const ModuleConfig& config) override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    ModuleState GetState() const override      { return m_State; }
    const char* GetLastError() const override  { return m_LastError.c_str(); }

    int Read (uint8_t* dst, size_t maxLen, uint32_t timeoutMs) override;
    int Write(const uint8_t* src, size_t len) override;

private:
    int         m_PinTX = -1;
    int         m_PinRX = -1;
    int         m_Port  = 1;
    uint32_t    m_Baud  = 9600;
    size_t      m_RxBufSize = 1024;

    ModuleState m_State = ModuleState::Uninitialized;
    std::string m_LastError;
};
