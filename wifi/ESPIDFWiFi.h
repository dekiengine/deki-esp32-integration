#pragma once

#include "IDekiWiFi.h"        // from deki-wifi
#include "ModuleConfig.h"
#include <string>

/**
 * @brief ESP-IDF implementation of IDekiWiFi.
 *
 * Drops into the active-driver slot via DekiWiFi::SetCurrent at module load
 * (see DekiESP32HALModule.cpp). Knows nothing about credentials, NVS, or
 * provisioning UX — the caller passes ssid/password to Connect explicitly.
 *
 * A small auto-connect helper elsewhere in this module reads NVS-stored
 * credentials at boot and calls Connect; that's the temporary placeholder
 * for the future captive-portal provisioning module.
 */
class ESPIDFWiFi : public IDekiWiFi
{
public:
    ESPIDFWiFi() = default;
    ~ESPIDFWiFi() override = default;

    // IDekiModule
    const char* GetModuleId()   const override { return "wifi"; }
    const char* GetModuleName() const override { return "WiFi (ESP-IDF)"; }
    void        Configure(const ModuleConfig&) override {}
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    ModuleState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    // IDekiWiFi
    bool Connect(const char* ssid, const char* password, uint32_t timeoutMs) override;
    void Disconnect() override;
    bool IsConnected() const override;
    int  ScanAPs(DekiAP* out, int maxCount) override;

private:
    ModuleState m_State     = ModuleState::Uninitialized;
    std::string m_LastError;
};
