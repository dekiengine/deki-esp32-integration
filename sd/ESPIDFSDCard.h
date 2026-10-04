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

class ESPIDFSDFileSystem;

#if defined(ESP32)
#else
struct sdmmc_card_t;
#endif

/// DekiSdCard::IDekiSDCard on ESP-IDF's SD host drivers (SPI or SDMMC) and
/// its VFS FAT filesystem. Once mounted, files are reached with standard
/// POSIX calls through ESP-IDF's VFS layer.
///
/// Pins (from PackageConfig):
/// - SPI: MOSI, MISO, CLK, CS
/// - SDMMC: CLK, CMD, D0, and D1-D3 in 4-bit mode
/// - CD: card detect (optional)
///
/// Settings:
/// - mode: "SPI", "SDMMC1Bit" or "SDMMC4Bit"
/// - auto_mount: "true" or "false"
/// - mount_point: filesystem mount point (default "/sdcard")
/// - spiHz / sdmmcHz: bus clock in Hz (default 20 MHz)
class ESPIDFSDCard : public DekiSdCard::IDekiSDCard
{
public:
    ESPIDFSDCard();
    ~ESPIDFSDCard() override;

    // Deki::IPackage
    const char* GetPackageId() const override { return "sd_card"; }
    const char* GetPackageName() const override { return "SD Card (ESP-IDF)"; }
    void Configure(const Deki::PackageConfig& config) override;
    bool Initialize() override;
    void Shutdown() override;
    void Update(float deltaTime) override;
    Deki::PackageState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    // DekiSdCard::IDekiSDCard
    bool Mount() override;
    void Unmount() override;
    DekiSdCard::SDCardState GetCardState() const override { return m_CardState; }
    bool IsCardInserted() const override;
    uint64_t GetTotalBytes() const override;
    uint64_t GetFreeBytes() const override;
    Deki::IFileSystem* GetFileSystem() override;
    const char* GetMountPoint() const override { return m_MountPoint.c_str(); }
    DekiSdCard::SDCardMode GetMode() const override { return m_Mode; }

    // Storage mode (USB MSC) is not available on plain ESP-IDF.
    bool SupportsStorageMode() const override { return false; }
    bool SetStorageMode(bool enabled) override
    {
        (void)enabled;
        return false;
    }
    bool IsStorageMode() const override { return false; }

private:
    // From PackageConfig
    int m_PinMOSI = -1;
    int m_PinMISO = -1;
    int m_PinCLK = -1;
    int m_PinCS = -1;
    int m_PinCD = -1;   // Card detect (optional, -1 when unused)
    int m_PinCMD = -1;  // SDMMC CMD pin
    int m_PinD0 = -1;   // SDMMC D0 pin
    int m_PinD1 = -1;   // SDMMC D1 pin (4-bit only)
    int m_PinD2 = -1;   // SDMMC D2 pin (4-bit only)
    int m_PinD3 = -1;   // SDMMC D3 pin (4-bit only)
    bool m_AutoMount = true;
    DekiSdCard::SDCardMode m_Mode = DekiSdCard::SDCardMode::SPI;
    uint32_t m_SpiFrequency = 20000000;    // Hz
    uint32_t m_SdmmcFrequency = 20000000;  // Hz
    std::string m_MountPoint = "/sdcard";

    // Runtime state
    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    DekiSdCard::SDCardState m_CardState = DekiSdCard::SDCardState::NotMounted;
    std::string m_LastError;
    bool m_Initialized = false;

    // ESP-IDF handles
    sdmmc_card_t* m_Card = nullptr;
    int m_SpiHostSlot = -1;
    bool m_OwnsSpiBus = false;  // false when joining a bus another device set up

    std::unique_ptr<ESPIDFSDFileSystem> m_FileSystem;

    // Whether the card detect pin reports a card
    bool CheckCardDetect() const;
};

}  // namespace DekiEsp32
