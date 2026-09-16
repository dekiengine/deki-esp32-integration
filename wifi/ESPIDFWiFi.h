#pragma once

#include "IDekiWiFi.h"        // from deki-wifi
#include <deki/PackageConfig.h>
#include <string>

namespace DekiEsp32
{

/**
 * @brief ESP-IDF implementation of DekiWifi::IDekiWiFi.
 *
 * Drops into the active-driver slot via DekiWifi::DekiWiFi::SetCurrent at package load
 * (see DekiESP32HALPackage.cpp). Knows nothing about credentials, NVS, or
 * provisioning UX — the caller passes ssid/password to Connect explicitly.
 *
 * A small auto-connect helper elsewhere in this package reads NVS-stored
 * credentials at boot and calls Connect; that's the temporary placeholder
 * for the future captive-portal provisioning package.
 */
class ESPIDFWiFi : public DekiWifi::IDekiWiFi
{
public:
    ESPIDFWiFi() = default;
    ~ESPIDFWiFi() override = default;

    // Deki::IPackage
    const char* GetPackageId()   const override { return "wifi"; }
    const char* GetPackageName() const override { return "WiFi (ESP-IDF)"; }
    void        Configure(const Deki::PackageConfig&) override {}
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    Deki::PackageState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    // DekiWifi::IDekiWiFi
    bool Connect(const char* ssid, const char* password, uint32_t timeoutMs) override;
    void Disconnect() override;
    bool IsConnected() const override;
    int  ScanAPs(DekiWifi::DekiAP* out, int maxCount) override;

private:
    Deki::PackageState m_State     = Deki::PackageState::Uninitialized;
    std::string m_LastError;
};

}  // namespace DekiEsp32
