#pragma once

#include "providers/IDekiSDCard.h"
#include "ModuleConfig.h"
#include <string>
#include <memory>

// Forward declarations
class ESPIDFSDFileSystem;

#if defined(ESP32)
#include "sd_protocol_types.h"
#else
struct sdmmc_card_t;
#endif

/**
 * @brief ESP-IDF native SPI SD card implementation of IDekiSDCard
 *
 * Uses ESP-IDF's native SPI SD host driver and VFS FAT filesystem
 * instead of the Arduino SD library. After mounting, files are accessible
 * via standard POSIX calls through ESP-IDF's VFS layer.
 *
 * Configuration pins (from ModuleConfig):
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
 * - spi_mhz: SPI clock frequency in MHz (1-40)
 */
class ESPIDFSDCard : public IDekiSDCard
{
public:
    ESPIDFSDCard();
    ~ESPIDFSDCard() override;

    // IDekiModule interface
    const char* GetModuleId() const override { return "sd_card"; }
    const char* GetModuleName() const override { return "SD Card (ESP-IDF)"; }
    void Configure(const ModuleConfig& config) override;
    bool Initialize() override;
    void Shutdown() override;
    void Update(float deltaTime) override;
    ModuleState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    // IDekiSDCard interface
    bool Mount() override;
    void Unmount() override;
    SDCardState GetCardState() const override { return m_CardState; }
    bool IsCardInserted() const override;
    uint64_t GetTotalBytes() const override;
    uint64_t GetFreeBytes() const override;
    IDekiFileSystem* GetFileSystem() override;
    const char* GetMountPoint() const override { return m_MountPoint.c_str(); }
    SDCardMode GetMode() const override { return m_Mode; }

    // Storage mode (USB MSC) - not supported on pure ESP-IDF
    bool SupportsStorageMode() const override { return false; }
    bool SetStorageMode(bool enabled) override { (void)enabled; return false; }
    bool IsStorageMode() const override { return false; }

private:
    // Configuration from ModuleConfig
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
    SDCardMode m_Mode = SDCardMode::SPI;
    uint32_t m_SpiFrequency = 20000000;  // SPI frequency in Hz (default 20 MHz)
    uint32_t m_SdmmcFrequency = 20000000; // SDMMC frequency in Hz (default 20 MHz)
    std::string m_MountPoint = "/sdcard";

    // Runtime state
    ModuleState m_State = ModuleState::Uninitialized;
    SDCardState m_CardState = SDCardState::NotMounted;
    std::string m_LastError;
    bool m_Initialized = false;

    // ESP-IDF specific handles
    sdmmc_card_t* m_Card = nullptr;
    int m_SpiHostSlot = -1;

    // Filesystem wrapper
    std::unique_ptr<ESPIDFSDFileSystem> m_FileSystem;

    // Helper to check card detect pin
    bool CheckCardDetect() const;
};

// Module metadata for editor UI generation
#ifdef DEKI_EDITOR
struct DekiModuleMeta;
extern const DekiModuleMeta* GetESPIDFSDCardMeta();
#endif
