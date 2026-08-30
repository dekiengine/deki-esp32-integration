#pragma once

#include "IDekiWiFi.h"        // from deki-wifi
#include "PackageConfig.h"
#include <string>

/**
 * @brief ESP-IDF implementation of IDekiWiFi.
 *
 * Drops into the active-driver slot via DekiWiFi::SetCurrent at package load
 * (see DekiESP32HALPackage.cpp). Knows nothing about credentials, NVS, or
 * provisioning UX — the caller passes ssid/password to Connect explicitly.
 *
 * A small auto-connect helper elsewhere in this package reads NVS-stored
 * credentials at boot and calls Connect; that's the temporary placeholder
 * for the future captive-portal provisioning package.
 */
class ESPIDFWiFi : public IDekiWiFi
{
public:
    ESPIDFWiFi() = default;
    ~ESPIDFWiFi() override = default;

    // IDekiPackage
    const char* GetPackageId()   const override { return "wifi"; }
    const char* GetPackageName() const override { return "WiFi (ESP-IDF)"; }
    void        Configure(const PackageConfig&) override {}
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    PackageState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    // IDekiWiFi
    bool Connect(const char* ssid, const char* password, uint32_t timeoutMs) override;
    void Disconnect() override;
    bool IsConnected() const override;
    int  ScanAPs(DekiAP* out, int maxCount) override;

private:
    PackageState m_State     = PackageState::Uninitialized;
    std::string m_LastError;
};
