#pragma once

#include "IDekiSDCard.h"  // from deki-sdcard
#include <deki/PackageConfig.h>
#include <string>
#include <memory>

#if defined(ESP32)
#include "sd_protocol_types.h"
#endif

namespace DekiEsp32
{

// Forward declarations
class ESPIDFSDFileSystem;

#if defined(ESP32)
#else
struct sdmmc_card_t;
#endif

/**
 * @brief ESP-IDF native SPI SD card implementation of DekiSdCard::IDekiSDCard
 *
 * Uses ESP-IDF's native SPI SD host driver and VFS FAT filesystem
 * instead of the Arduino SD library. After mounting, files are accessible
 * via standard POSIX calls through ESP-IDF's VFS layer.
 *
 * Configuration pins (from PackageConfig):
 * - MOSI: SPI Master Out (data to card)
 * - MISO: SPI Master In (data from card)
 * - CLK: SPI Clock
 * - CS: Chip Select
 * - CD: Card Detect (optional)
 *
 * Configuration settings:
 * - mode: "SPI" (only SPI supported currently)
 * - auto_mount: "true" or "false"
 * - mount_point: Filesystem mount point (default "/sdcard")
 * - spiHz: SPI clock frequency in MHz (1-40)
 */
class ESPIDFSDCard : public DekiSdCard::IDekiSDCard
{
public:
    ESPIDFSDCard();
    ~ESPIDFSDCard() override;

    // Deki::IPackage interface
    const char* GetPackageId() const override { return "sd_card"; }
    const char* GetPackageName() const override { return "SD Card (ESP-IDF)"; }
    void Configure(const Deki::PackageConfig& config) override;
    bool Initialize() override;
    void Shutdown() override;
    void Update(float deltaTime) override;
    Deki::PackageState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    // DekiSdCard::IDekiSDCard interface
    bool Mount() override;
    void Unmount() override;
    DekiSdCard::SDCardState GetCardState() const override { return m_CardState; }
    bool IsCardInserted() const override;
    uint64_t GetTotalBytes() const override;
    uint64_t GetFreeBytes() const override;
    Deki::IFileSystem* GetFileSystem() override;
    const char* GetMountPoint() const override { return m_MountPoint.c_str(); }
    DekiSdCard::SDCardMode GetMode() const override { return m_Mode; }

    // Storage mode (USB MSC) - not supported on pure ESP-IDF
    bool SupportsStorageMode() const override { return false; }
    bool SetStorageMode(bool enabled) override { (void)enabled; return false; }
    bool IsStorageMode() const override { return false; }

private:
    // Configuration from PackageConfig
    int m_PinMOSI = -1;
    int m_PinMISO = -1;
    int m_PinCLK = -1;
    int m_PinCS = -1;
    int m_PinCD = -1;  // Card detect (optional, -1 if not used)
    int m_PinCMD = -1; // SDMMC CMD pin
    int m_PinD0 = -1;  // SDMMC D0 pin
    int m_PinD1 = -1;  // SDMMC D1 pin (4-bit only)
    int m_PinD2 = -1;  // SDMMC D2 pin (4-bit only)
    int m_PinD3 = -1;  // SDMMC D3 pin (4-bit only)
    bool m_AutoMount = true;
    DekiSdCard::SDCardMode m_Mode = DekiSdCard::SDCardMode::SPI;
    uint32_t m_SpiFrequency = 20000000;  // SPI frequency in Hz (default 20 MHz)
    uint32_t m_SdmmcFrequency = 20000000; // SDMMC frequency in Hz (default 20 MHz)
    std::string m_MountPoint = "/sdcard";

    // Runtime state
    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    DekiSdCard::SDCardState m_CardState = DekiSdCard::SDCardState::NotMounted;
    std::string m_LastError;
    bool m_Initialized = false;

    // ESP-IDF specific handles
    sdmmc_card_t* m_Card = nullptr;
    int m_SpiHostSlot = -1;
    bool m_OwnsSpiBus = false;  // false when joining a bus another device set up

    // Filesystem wrapper
    std::unique_ptr<ESPIDFSDFileSystem> m_FileSystem;

    // Helper to check card detect pin
    bool CheckCardDetect() const;
};

}  // namespace DekiEsp32

