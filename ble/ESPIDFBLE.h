#pragma once

#include "IDekiBLE.h"        // from deki-ble
#include "PackageConfig.h"
#include <string>

/**
 * @brief NimBLE implementation of IDekiBLE.
 *
 * Drops into the active-driver slot via DekiBLE::SetCurrent at package load
 * (see ESP32HALPackage.cpp). Backed by ESP-IDF's NimBLE host stack (the
 * `nimble` component). BLE-only by design; no Bluetooth Classic.
 *
 * Bonding / SMP key persistence are intentionally left at NimBLE defaults
 * (Just Works, no IO capability, no bond persisted). Higher layers that
 * need encryption or authenticated pairing should extend the interface
 * or sit beside this package.
 */
class ESPIDFBLE : public IDekiBLE
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

    // IDekiBLE -- scan
    bool StartScan(uint16_t intervalMs, uint16_t windowMs, bool active, uint32_t durationMs) override;
    void StopScan() override;
    void SetScanCallback(DekiBLEScanCb cb, void* user) override;

    // IDekiBLE -- advertise
    bool StartAdvertising(const DekiBLEAdvData& data) override;
    void StopAdvertising() override;
    bool IsAdvertising() const override;

    // IDekiBLE -- GATT server
    bool BuildGattServer(DekiBLEServiceSpec* services, uint8_t count) override;
    bool NotifyValue(DekiBLEConnHandle conn, DekiBLECharHandle handle, const void* data, size_t len) override;
    void SetCharWriteCallback(DekiBLECharWriteCb cb, void* user) override;
    void SetCharReadCallback (DekiBLECharReadCb  cb, void* user) override;
    void SetConnectionCallback(DekiBLEConnCb cb, void* user) override;

    // IDekiBLE -- GATT client
    bool Connect(const DekiBLEAddress& addr, uint32_t timeoutMs) override;
    void DisconnectClient(DekiBLEConnHandle conn) override;
    bool DiscoverService(DekiBLEConnHandle conn, const DekiBLEUUID& service,
                         DekiBLECharHandle* outFirstHandle, uint8_t* outCount) override;
    bool ReadRemote(DekiBLEConnHandle conn, DekiBLECharHandle handle, uint8_t* out, size_t* len) override;
    bool WriteRemote(DekiBLEConnHandle conn, DekiBLECharHandle handle, const void* data, size_t len, bool with_response) override;
    bool Subscribe(DekiBLEConnHandle conn, DekiBLECharHandle handle, bool enable) override;
    void SetNotifyCallback(DekiBLENotifyCb cb, void* user) override;

private:
    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    std::string m_LastError;
};
