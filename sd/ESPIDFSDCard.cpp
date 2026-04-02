#include "ESPIDFSDCard.h"
#include "ESPIDFSDFileSystem.h"

#ifdef DEKI_EDITOR
#include "../../DekiModuleMeta.h"
#endif

// ESP-IDF native SD card APIs
#if defined(ESP32)
#include "driver/sdspi_host.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_protocol_types.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#endif // ESP32

#if defined(ESP32)
#include "esp_log.h"
static const char* TAG = "ESPIDFSD";
#endif

// ============================================================================
// Module Metadata (for editor UI generation)
// ============================================================================

#ifdef DEKI_EDITOR
static const DekiModulePinInfo s_ESPIDFSDCardPins[] = {
    {"MOSI", "SPI Master Out (data to card)", true, -1},
    {"MISO", "SPI Master In (data from card)", true, -1},
    {"CLK", "Clock (shared SPI/SDMMC)", true, -1},
    {"CS", "SPI Chip Select", true, -1},
    {"CMD", "SDMMC Command line", false, -1},
    {"D0", "SDMMC Data line 0", false, -1},
    {"D1", "SDMMC Data line 1 (4-bit)", false, -1},
    {"D2", "SDMMC Data line 2 (4-bit)", false, -1},
    {"D3", "SDMMC Data line 3 (4-bit)", false, -1},
    {"CD", "Card Detect (optional)", false, -1},
};

static const char* s_ESPIDFSDCardModes[] = {"SPI", "SDMMC_1BIT", nullptr};

static const DekiModuleSettingInfo s_ESPIDFSDCardSettings[] = {
    {"mode", "enum", "Interface mode", "SPI", s_ESPIDFSDCardModes, 2},
    {"auto_mount", "bool", "Automatically mount card on initialization", "true", nullptr, 0},
    {"mount_point", "string", "Virtual filesystem mount point", "/sdcard", nullptr, 0},
    {"spi_mhz", "int", "SPI clock frequency in MHz (1-40)", "20", nullptr, 0},
};

static const DekiModuleMeta s_ESPIDFSDCardMeta = {
    "esp_idf_sd",       // Module ID
    "ESP-IDF SD",       // Display name
    "storage",          // Category
    "SD card filesystem using ESP-IDF SPI or SDMMC APIs",
    s_ESPIDFSDCardPins, 10,
    s_ESPIDFSDCardSettings, 4,
    []() -> IDekiModule* { return new ESPIDFSDCard(); }
};

const DekiModuleMeta* GetESPIDFSDCardMeta()
{
    return &s_ESPIDFSDCardMeta;
}
#endif // DEKI_EDITOR

// ============================================================================
// ESPIDFSDCard Implementation
// ============================================================================

ESPIDFSDCard::ESPIDFSDCard()
{
}

ESPIDFSDCard::~ESPIDFSDCard()
{
    Shutdown();
}

void ESPIDFSDCard::Configure(const ModuleConfig& config)
{
    m_PinCLK = config.GetPin("CLK", -1);
    m_PinCD = config.GetPin("CD", -1);

    m_AutoMount = config.GetBool("auto_mount", true);
    m_MountPoint = config.GetString("mount_point", "/sdcard");

    // Determine mode
    std::string modeStr = config.GetString("mode", "SPI");
    if (modeStr == "SDMMC_4BIT")
    {
        m_Mode = SDCardMode::SDMMC_4BIT;
        m_PinCMD = config.GetPin("CMD", -1);
        m_PinD0 = config.GetPin("D0", -1);
        m_PinD1 = config.GetPin("D1", -1);
        m_PinD2 = config.GetPin("D2", -1);
        m_PinD3 = config.GetPin("D3", -1);
        int sdmmcMhz = config.GetInt("sdmmc_mhz", 20);
        m_SdmmcFrequency = static_cast<uint32_t>(sdmmcMhz) * 1000000;
    }
    else if (modeStr == "SDMMC_1BIT")
    {
        m_Mode = SDCardMode::SDMMC_1BIT;
        m_PinCMD = config.GetPin("CMD", -1);
        m_PinD0 = config.GetPin("D0", -1);
        int sdmmcMhz = config.GetInt("sdmmc_mhz", 20);
        m_SdmmcFrequency = static_cast<uint32_t>(sdmmcMhz) * 1000000;
    }
    else
    {
        m_Mode = SDCardMode::SPI;
        m_PinMOSI = config.GetPin("MOSI", -1);
        m_PinMISO = config.GetPin("MISO", -1);
        m_PinCS = config.GetPin("CS", -1);

        // SPI frequency in MHz (convert to Hz)
        int spiMhz = config.GetInt("spi_mhz", 20);
        m_SpiFrequency = static_cast<uint32_t>(spiMhz) * 1000000;
    }
}

bool ESPIDFSDCard::Initialize()
{
    m_State = ModuleState::Uninitialized;
    m_LastError.clear();

#if defined(ESP32)
    if (m_Mode == SDCardMode::SDMMC_4BIT)
    {
        ESP_LOGI(TAG, "Initialize SDMMC 4-bit: CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d, CD=%d",
                 m_PinCLK, m_PinCMD, m_PinD0, m_PinD1, m_PinD2, m_PinD3, m_PinCD);

        if (m_PinCLK < 0 || m_PinCMD < 0 || m_PinD0 < 0 || m_PinD1 < 0 || m_PinD2 < 0 || m_PinD3 < 0)
        {
            m_LastError = "SDMMC 4-bit requires CLK, CMD, D0, D1, D2, and D3 pins";
            m_State = ModuleState::Error;
            return false;
        }
    }
    else if (m_Mode == SDCardMode::SDMMC_1BIT)
    {
        ESP_LOGI(TAG, "Initialize SDMMC 1-bit: CLK=%d, CMD=%d, D0=%d, CD=%d",
                 m_PinCLK, m_PinCMD, m_PinD0, m_PinCD);

        if (m_PinCLK < 0 || m_PinCMD < 0 || m_PinD0 < 0)
        {
            m_LastError = "SDMMC 1-bit requires CLK, CMD, and D0 pins";
            m_State = ModuleState::Error;
            return false;
        }
    }
    else
    {
        ESP_LOGI(TAG, "Initialize SPI: MOSI=%d, MISO=%d, CLK=%d, CS=%d, CD=%d, SPI=%d MHz",
                 m_PinMOSI, m_PinMISO, m_PinCLK, m_PinCS, m_PinCD,
                 static_cast<int>(m_SpiFrequency / 1000000));

        // Validate required pins
        if (m_PinCS < 0)
        {
            m_LastError = "CS pin not configured";
            m_State = ModuleState::Error;
            return false;
        }

        if (m_PinMOSI >= 0 && m_PinMISO >= 0 && m_PinCLK >= 0)
        {
            // SD card SPI pre-conditioning: bit-bang 80+ clock pulses with CS HIGH.
            // Required by SD Physical Layer Spec to reset the card's SPI state machine.
            // If the card was mid-transaction when the MCU reset (or from a previous
            // session), it can be stuck waiting for clocks. Arduino's SD.begin() does
            // this internally via card.init() — ESP-IDF's sdspi driver may not.
            {
                gpio_num_t cs = static_cast<gpio_num_t>(m_PinCS);
                gpio_num_t clk = static_cast<gpio_num_t>(m_PinCLK);
                gpio_num_t mosi = static_cast<gpio_num_t>(m_PinMOSI);

                gpio_set_direction(cs, GPIO_MODE_OUTPUT);
                gpio_set_direction(clk, GPIO_MODE_OUTPUT);
                gpio_set_direction(mosi, GPIO_MODE_OUTPUT);

                gpio_set_level(cs, 1);    // CS HIGH (deselected)
                gpio_set_level(mosi, 1);  // MOSI HIGH (0xFF)
                gpio_set_level(clk, 0);   // CLK idle low (SPI mode 0)

                vTaskDelay(pdMS_TO_TICKS(10));  // Let card power stabilize

                // 80 full clock cycles at ~100 KHz
                for (int i = 0; i < 80; i++)
                {
                    gpio_set_level(clk, 1);
                    esp_rom_delay_us(5);
                    gpio_set_level(clk, 0);
                    esp_rom_delay_us(5);
                }

                ESP_LOGI(TAG, "SD card pre-conditioning complete (80 clocks with CS HIGH)");
            }
            // spi_bus_initialize will reconfigure these pins for the SPI peripheral

            spi_bus_config_t bus_cfg = {};
            bus_cfg.mosi_io_num = m_PinMOSI;
            bus_cfg.miso_io_num = m_PinMISO;
            bus_cfg.sclk_io_num = m_PinCLK;
            bus_cfg.quadwp_io_num = -1;
            bus_cfg.quadhd_io_num = -1;
            bus_cfg.max_transfer_sz = 4000;

            esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
            if (ret != ESP_OK)
            {
                m_LastError = "SPI bus initialization failed";
                m_State = ModuleState::Error;
                ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
                return false;
            }
            m_SpiHostSlot = SPI2_HOST;
        }
    }

    // Configure card detect pin as input with pullup (if specified)
    if (m_PinCD >= 0)
    {
        gpio_set_direction(static_cast<gpio_num_t>(m_PinCD), GPIO_MODE_INPUT);
        gpio_set_pull_mode(static_cast<gpio_num_t>(m_PinCD), GPIO_PULLUP_ONLY);
    }
#endif

    m_Initialized = true;
    m_State = ModuleState::Initialized;

    // Create filesystem wrapper
    m_FileSystem = std::make_unique<ESPIDFSDFileSystem>(this);

    // Auto-mount if enabled
    if (m_AutoMount)
    {
        if (!Mount())
        {
            // Mount failed, but module is still initialized
            // Card might not be inserted yet
        }
    }

    return true;
}

void ESPIDFSDCard::Shutdown()
{
    if (m_Initialized)
    {
        Unmount();
        m_FileSystem.reset();

#if defined(ESP32)
        if (m_Mode == SDCardMode::SPI && m_SpiHostSlot >= 0)
        {
            spi_bus_free(static_cast<spi_host_device_t>(m_SpiHostSlot));
            m_SpiHostSlot = -1;
        }
#endif

        m_Initialized = false;
    }
    m_State = ModuleState::Disabled;
}

void ESPIDFSDCard::Update(float deltaTime)
{
    (void)deltaTime;

    // Check for card insertion/removal via card detect pin
    if (m_PinCD >= 0 && m_Initialized)
    {
        bool inserted = CheckCardDetect();

        if (inserted && m_CardState == SDCardState::NotMounted && m_AutoMount)
        {
            // Card was inserted, try to mount
            Mount();
        }
        else if (!inserted && m_CardState == SDCardState::Mounted)
        {
            // Card was removed, unmount
            Unmount();
        }
    }
}

bool ESPIDFSDCard::Mount()
{
    if (m_CardState == SDCardState::Mounted)
        return true;

    m_CardState = SDCardState::Mounting;
    m_LastError.clear();

#if defined(ESP32)
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    esp_err_t ret;

    if (m_Mode == SDCardMode::SDMMC_1BIT || m_Mode == SDCardMode::SDMMC_4BIT)
    {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = static_cast<int>(m_SdmmcFrequency / 1000);

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.clk = static_cast<gpio_num_t>(m_PinCLK);
        slot_config.cmd = static_cast<gpio_num_t>(m_PinCMD);
        slot_config.d0 = static_cast<gpio_num_t>(m_PinD0);

        if (m_Mode == SDCardMode::SDMMC_4BIT)
        {
            slot_config.width = 4;
            slot_config.d1 = static_cast<gpio_num_t>(m_PinD1);
            slot_config.d2 = static_cast<gpio_num_t>(m_PinD2);
            slot_config.d3 = static_cast<gpio_num_t>(m_PinD3);
            ESP_LOGI(TAG, "Mounting SD card SDMMC 4-bit (CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d)...",
                     m_PinCLK, m_PinCMD, m_PinD0, m_PinD1, m_PinD2, m_PinD3);
        }
        else
        {
            host.flags = SDMMC_HOST_FLAG_1BIT;
            slot_config.width = 1;
            ESP_LOGI(TAG, "Mounting SD card SDMMC 1-bit (CLK=%d, CMD=%d, D0=%d)...",
                     m_PinCLK, m_PinCMD, m_PinD0);
        }

        if (m_PinCD >= 0)
        {
            slot_config.cd = static_cast<gpio_num_t>(m_PinCD);
        }

        ret = esp_vfs_fat_sdmmc_mount(
            m_MountPoint.c_str(), &host, &slot_config, &mount_config, &m_Card);
    }
    else
    {
        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.gpio_cs = static_cast<gpio_num_t>(m_PinCS);
        slot_config.host_id = static_cast<spi_host_device_t>(m_SpiHostSlot >= 0 ? m_SpiHostSlot : SPI2_HOST);

        if (m_PinCD >= 0)
        {
            slot_config.gpio_cd = static_cast<gpio_num_t>(m_PinCD);
        }

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.max_freq_khz = static_cast<int>(m_SpiFrequency / 1000);

        ESP_LOGI(TAG, "Mounting SD card SPI (max %d KHz)...", host.max_freq_khz);
        ret = esp_vfs_fat_sdspi_mount(
            m_MountPoint.c_str(), &host, &slot_config, &mount_config, &m_Card);
    }

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "SD card mounted at %s", m_MountPoint.c_str());
        sdmmc_card_print_info(stdout, m_Card);
        m_CardState = SDCardState::Mounted;
        return true;
    }

    ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
    m_Card = nullptr;
    m_CardState = SDCardState::Error;
    m_LastError = "Mount failed";
    return false;
#else
    // Non-ESP32 platform - return success for editor simulation
    m_CardState = SDCardState::Mounted;
    return true;
#endif
}

void ESPIDFSDCard::Unmount()
{
    if (m_CardState != SDCardState::Mounted)
        return;

#if defined(ESP32)
    if (m_Card)
    {
        esp_vfs_fat_sdcard_unmount(m_MountPoint.c_str(), m_Card);
        ESP_LOGI(TAG, "SD card unmounted");
        m_Card = nullptr;
    }
#endif

    m_CardState = SDCardState::NotMounted;
}

bool ESPIDFSDCard::IsCardInserted() const
{
    if (m_PinCD >= 0)
    {
        return CheckCardDetect();
    }

    // No card detect pin - assume inserted if mounted
    return m_CardState == SDCardState::Mounted;
}

bool ESPIDFSDCard::CheckCardDetect() const
{
#if defined(ESP32)
    if (m_PinCD >= 0)
    {
        // Card detect pins are typically active LOW (pulled low when card inserted)
        return gpio_get_level(static_cast<gpio_num_t>(m_PinCD)) == 0;
    }
#endif
    return true;  // Assume inserted if no CD pin
}

uint64_t ESPIDFSDCard::GetTotalBytes() const
{
    if (m_CardState != SDCardState::Mounted)
        return 0;

#if defined(ESP32)
    if (m_Card)
    {
        return static_cast<uint64_t>(m_Card->csd.capacity) * m_Card->csd.sector_size;
    }
#endif
    return 0;
}

uint64_t ESPIDFSDCard::GetFreeBytes() const
{
    if (m_CardState != SDCardState::Mounted)
        return 0;

#if defined(ESP32)
    uint64_t totalBytes = 0, freeBytes = 0;
    if (esp_vfs_fat_info(m_MountPoint.c_str(), &totalBytes, &freeBytes) == ESP_OK)
        return freeBytes;
#endif
    return 0;
}

IDekiFileSystem* ESPIDFSDCard::GetFileSystem()
{
    if (m_CardState != SDCardState::Mounted)
        return nullptr;

    return m_FileSystem.get();
}
