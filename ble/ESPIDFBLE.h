#pragma once

#include "IDekiBLE.h"        // from deki-ble
#include <deki/PackageConfig.h>
#include <string>

namespace DekiEsp32
{

/**
 * @brief NimBLE implementation of DekiBle::IDekiBLE.
 *
 * Drops into the active-driver slot via DekiBle::DekiBLE::SetCurrent at package load
 * (see ESP32HALPackage.cpp). Backed by ESP-IDF's NimBLE host stack (the
 * `nimble` component). BLE-only by design; no Bluetooth Classic.
 *
 * Bonding / SMP key persistence are intentionally left at NimBLE defaults
 * (Just Works, no IO capability, no bond persisted). Higher layers that
 * need encryption or authenticated pairing should extend the interface
 * or sit beside this package.
 */
class ESPIDFBLE : public DekiBle::IDekiBLE
{
public:
    ESPIDFBLE() = default;
    ~ESPIDFBLE() override = default;

    // Deki::IPackage
    const char* GetPackageId()   const override { return "ble"; }
    const char* GetPackageName() const override { return "BLE (NimBLE)"; }
    void        Configure(const Deki::PackageConfig&) override {}
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    Deki::PackageState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    // DekiBle::IDekiBLE -- scan
    bool StartScan(uint16_t intervalMs, uint16_t windowMs, bool active, uint32_t durationMs) override;
    void StopScan() override;
    void SetScanCallback(DekiBle::DekiBLEScanCb cb, void* user) override;

    // DekiBle::IDekiBLE -- advertise
    bool StartAdvertising(const DekiBle::DekiBLEAdvData& data) override;
    void StopAdvertising() override;
    bool IsAdvertising() const override;

    // DekiBle::IDekiBLE -- GATT server
    bool BuildGattServer(DekiBle::DekiBLEServiceSpec* services, uint8_t count) override;
    bool NotifyValue(DekiBle::DekiBLEConnHandle conn, DekiBle::DekiBLECharHandle handle, const void* data, size_t len) override;
    void SetCharWriteCallback(DekiBle::DekiBLECharWriteCb cb, void* user) override;
    void SetCharReadCallback (DekiBle::DekiBLECharReadCb  cb, void* user) override;
    void SetConnectionCallback(DekiBle::DekiBLEConnCb cb, void* user) override;

    // DekiBle::IDekiBLE -- GATT client
    bool Connect(const DekiBle::DekiBLEAddress& addr, uint32_t timeoutMs) override;
    void DisconnectClient(DekiBle::DekiBLEConnHandle conn) override;
    bool DiscoverService(DekiBle::DekiBLEConnHandle conn, const DekiBle::DekiBLEUUID& service,
                         DekiBle::DekiBLECharHandle* outFirstHandle, uint8_t* outCount) override;
    bool ReadRemote(DekiBle::DekiBLEConnHandle conn, DekiBle::DekiBLECharHandle handle, uint8_t* out, size_t* len) override;
    bool WriteRemote(DekiBle::DekiBLEConnHandle conn, DekiBle::DekiBLECharHandle handle, const void* data, size_t len, bool with_response) override;
    bool Subscribe(DekiBle::DekiBLEConnHandle conn, DekiBle::DekiBLECharHandle handle, bool enable) override;
    void SetNotifyCallback(DekiBle::DekiBLENotifyCb cb, void* user) override;

private:
    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    std::string m_LastError;
};

}  // namespace DekiEsp32
