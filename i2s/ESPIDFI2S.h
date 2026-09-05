#pragma once

#include "IDekiI2S.h"  // from deki-i2s
#include "PackageConfig.h"
#include <string>

#if defined(ESP32)
#include "driver/i2s_std.h"
#endif

class ESPIDFI2S : public IDekiI2S
{
public:
    ESPIDFI2S() = default;
    ~ESPIDFI2S() override;

    const char* GetPackageId() const override   { return "i2s"; }
    const char* GetPackageName() const override { return "I2S (ESP-IDF)"; }
    void        Configure(const Deki::PackageConfig& config) override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    Deki::PackageState GetState() const override      { return m_State; }
    const char* GetLastError() const override  { return m_LastError.c_str(); }

    int  GetPort() const override { return m_Port; }
    int  Write(const void* data, size_t bytes, uint32_t timeoutMs) override;
    bool Start() override;
    bool Stop()  override;

private:
    int         m_PinBCLK   = -1;
    int         m_PinLRCLK  = -1;
    int         m_PinDOUT   = -1;
    int         m_Port      = 0;
    int         m_SampleRate = 16000;
    int         m_BitsPerSample = 16;
    int         m_Channels = 1;

    bool        m_Running = false;
    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    std::string m_LastError;

#if defined(ESP32)
    i2s_chan_handle_t m_TxHandle = nullptr;
#endif
};
