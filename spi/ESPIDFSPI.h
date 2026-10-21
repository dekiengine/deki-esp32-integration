#pragma once

#include "IDekiSPI.h"  // from deki-spi
#include <deki/PackageConfig.h>
#include <memory>
#include <string>
#include <vector>

namespace DekiEsp32
{

/// A device on an ESP-IDF SPI bus: one spi_master device handle.
///
/// Queued transfers go to the driver's own queue and out by DMA one after the
/// other, started from its interrupt, so chunks of pixels follow each other
/// with no gap but the driver's (about 19 us each on an ESP32-S3 at 80 MHz).
/// The data/command pin is set in the driver's pre-transfer callback, from
/// the level stored in the transaction, so it changes exactly as that
/// transfer starts.
class ESPIDFSPIDevice : public DekiSpi::IDekiSPIDevice
{
public:
    ESPIDFSPIDevice(void* handle, const DekiSpi::DeviceConfig& config, size_t maxTransferBytes);
    ~ESPIDFSPIDevice() override;

    bool Transmit(const DekiSpi::Transfer& t) override;
    bool Queue(const DekiSpi::Transfer& t) override;
    bool Wait(size_t leaveInFlight, uint32_t timeoutMs) override;
    size_t InFlight() const override { return m_InFlight; }
    bool Acquire() override;
    void Release() override;
    size_t GetMaxTransferBytes() const override { return m_MaxTransferBytes; }
    uint32_t GetClockHz() const override;

private:
    // void* keeps driver/spi_master.h out of the desktop build.
    void* m_Handle = nullptr;
    int32_t m_DcPin = -1;
    bool m_HalfDuplex = true;
    size_t m_MaxTransferBytes = 0;
    // One spi_transaction_ext_t per queue slot: the driver keeps a pointer to
    // a queued transaction until it is waited for. Transfers finish in the
    // order they were queued, so slots are reused round the ring.
    std::unique_ptr<uint8_t[]> m_Slots;
    size_t m_SlotCount = 0;
    size_t m_NextSlot = 0;
    size_t m_InFlight = 0;
    int m_AcquireDepth = 0;
};

/// An SPI bus on ESP-IDF's spi_master driver.
///
/// A bus another driver has already started (an SD card, or LovyanGFX's
/// display driver) is joined rather than started again: ESP-IDF arbitrates
/// between the devices of one bus. A joined bus keeps the longest transfer
/// the driver that started it chose, which is why SPIBusComponent goes
/// before every device in the boot scene.
class ESPIDFSPIBus : public DekiSpi::IDekiSPIBus
{
public:
    ESPIDFSPIBus() = default;
    ~ESPIDFSPIBus() override;

    const char* GetPackageId() const override { return "spi"; }
    const char* GetPackageName() const override { return "SPI (ESP-IDF)"; }
    void Configure(const Deki::PackageConfig& config) override;
    bool Initialize() override;
    void Shutdown() override;
    void Update(float) override {}
    Deki::PackageState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    int GetHost() const override { return m_Host; }
    int GetSclkPin() const override { return m_PinSclk; }
    int GetMosiPin() const override { return m_PinMosi; }
    int GetMisoPin() const override { return m_PinMiso; }
    bool IsQuad() const override { return m_PinIo2 >= 0 && m_PinIo3 >= 0; }
    size_t GetMaxTransferBytes() const override { return m_MaxTransferBytes; }
    DekiSpi::IDekiSPIDevice* AddDevice(const DekiSpi::DeviceConfig& config) override;

private:
    int m_Host = 1;
    int m_PinSclk = -1;
    int m_PinMosi = -1;
    int m_PinMiso = -1;
    int m_PinIo2 = -1;
    int m_PinIo3 = -1;
    size_t m_MaxTransferBytes = 32768;
    // False when another driver had started the bus: it is that driver's to
    // free.
    bool m_OwnsBus = false;
    bool m_Started = false;
    std::vector<std::unique_ptr<ESPIDFSPIDevice>> m_Devices;

    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    std::string m_LastError;
};

}  // namespace DekiEsp32
