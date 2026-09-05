#pragma once

#include "IDekiUART.h"  // from deki-uart
#include "PackageConfig.h"
#include <string>

class ESPIDFUART : public IDekiUART
{
public:
    ESPIDFUART() = default;
    ~ESPIDFUART() override = default;

    const char* GetPackageId() const override   { return "uart"; }
    const char* GetPackageName() const override { return "UART (ESP-IDF)"; }
    void        Configure(const Deki::PackageConfig& config) override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    Deki::PackageState GetState() const override      { return m_State; }
    const char* GetLastError() const override  { return m_LastError.c_str(); }

    int Read (uint8_t* dst, size_t maxLen, uint32_t timeoutMs) override;
    int Write(const uint8_t* src, size_t len) override;

private:
    int         m_PinTX = -1;
    int         m_PinRX = -1;
    int         m_Port  = 1;
    uint32_t    m_Baud  = 9600;
    size_t      m_RxBufSize = 1024;

    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    std::string m_LastError;
};
