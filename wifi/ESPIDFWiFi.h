#pragma once

#include "IDekiWiFi.h"  // from deki-wifi
#include <deki/PackageConfig.h>
#include <string>

namespace DekiEsp32
{

/// DekiWifi::IDekiWiFi on ESP-IDF.
///
/// Becomes the active driver through DekiWifi::DekiWiFi::SetCurrent at
/// package load (see ESP32HALPackage.cpp). Keeps no credentials and has no
/// provisioning UI: the caller passes ssid and password to Connect.
class ESPIDFWiFi : public DekiWifi::IDekiWiFi
{
public:
    ESPIDFWiFi() = default;
    ~ESPIDFWiFi() override = default;

    // Deki::IPackage
    const char* GetPackageId() const override { return "wifi"; }
    const char* GetPackageName() const override { return "WiFi (ESP-IDF)"; }
    void Configure(const Deki::PackageConfig&) override {}
    bool Initialize() override;
    void Shutdown() override;
    void Update(float) override {}
    Deki::PackageState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    // DekiWifi::IDekiWiFi
    bool Connect(const char* ssid, const char* password, uint32_t timeoutMs) override;
    void Disconnect() override;
    bool IsConnected() const override;
    int ScanAPs(DekiWifi::DekiAP* out, int maxCount) override;

private:
    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    std::string m_LastError;
};

}  // namespace DekiEsp32
