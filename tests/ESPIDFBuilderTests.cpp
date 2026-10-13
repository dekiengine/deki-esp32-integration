// What the ESP-IDF builder checks and writes before idf.py runs: the platform
// values that reach a shell line, the sdkconfig defaults each board option
// turns into, and the partition table.
//
// None of it needs ESP-IDF installed. A mistake here shows up on a board as a
// build that ignores a setting, a partition table that does not fit the
// flash, or a stale sdkconfig that silently wins, so it is worth pinning.

#include <gtest/gtest.h>

#include "editor/ESPIDFBuilder.h"

#include <deki-editor/build/PlatformConfig.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace DekiEditor;
namespace fs = std::filesystem;

namespace
{

PlatformConfig Board(const std::string& target = "esp32s3")
{
    PlatformConfig config;
    config.id = "test_board";
    config.displayName = "Test Board";
    config.framework = "espidf";
    config.SetOption("idfTarget", target);
    return config;
}

bool HasProblemMentioning(const std::vector<std::string>& problems, const std::string& word)
{
    for (const auto& p : problems)
    {
        if (p.find(word) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

std::string ReadFile(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

bool Contains(const std::string& text, const std::string& line)
{
    return text.find(line) != std::string::npos;
}

}  // namespace

// --- Validation ---

TEST(ESPIDFBuilder, AKnownTargetIsValid)
{
    ESPIDFBuilder builder;
    for (const char* t : { "esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6", "esp32h2" })
    {
        EXPECT_TRUE(builder.ValidatePlatform(Board(t)).empty()) << t;
    }
}

TEST(ESPIDFBuilder, AMissingOrUnknownTargetIsRefused)
{
    // idfTarget reaches the line that runs idf.py, and a platform can come in
    // a board pack, so it is checked against the chips ESP-IDF knows.
    ESPIDFBuilder builder;
    PlatformConfig none = Board();
    none.SetOption("idfTarget", "");
    EXPECT_TRUE(HasProblemMentioning(builder.ValidatePlatform(none), "idfTarget"));
    EXPECT_TRUE(HasProblemMentioning(builder.ValidatePlatform(Board("esp32s3; rm -rf ~")), "idfTarget"));
    EXPECT_TRUE(HasProblemMentioning(builder.ValidatePlatform(Board("esp8266")), "idfTarget"));
}

TEST(ESPIDFBuilder, DisplayBusMustBeAnIdentifier)
{
    ESPIDFBuilder builder;
    for (const char* ok : { "SPI", "PARALLEL_8BIT", "SOFTWARE" })
    {
        PlatformConfig c = Board();
        c.SetOption("displayBus", ok);
        EXPECT_TRUE(builder.ValidatePlatform(c).empty()) << ok;
    }
    for (const char* bad : { "spi", "SPI BUS", "SPI;echo", "$(X)", std::string(33, 'A').c_str() })
    {
        PlatformConfig c = Board();
        c.SetOption("displayBus", bad);
        EXPECT_TRUE(HasProblemMentioning(builder.ValidatePlatform(c), "displayBus")) << bad;
    }
}

// --- Generated files ---

namespace
{

class ESPIDFBuildFilesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_Project = fs::temp_directory_path() /
                    ("deki-espidf-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(m_Project);
        fs::create_directories(m_Project / "assets");
        fs::create_directories(m_Project / "src");
        std::ofstream(m_Project / "deki.json") << R"({"name":"t","engine":"deki"})";
        std::ofstream(m_Project / "deki-packages.json") << R"({"packages":{}})";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_Project, ec);
    }

    // Generates and returns the board's directory under the build directory.
    fs::path Generate(const PlatformConfig& config)
    {
        EXPECT_TRUE(builder.GenerateBuildFiles(m_Project.string(), config, {}));
        return fs::path(builder.GetBuildDirectory(m_Project.string())) / config.id;
    }

    std::string Sdkconfig(const PlatformConfig& config) { return ReadFile(Generate(config) / "sdkconfig.defaults"); }

    ESPIDFBuilder builder;
    fs::path m_Project;
};

}  // namespace

TEST_F(ESPIDFBuildFilesTest, SdkconfigNamesTheTargetAndThePartitionTable)
{
    const std::string s = Sdkconfig(Board("esp32c3"));
    EXPECT_TRUE(Contains(s, "CONFIG_IDF_TARGET=\"esp32c3\"\n"));
    EXPECT_TRUE(Contains(s, "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"test_board/partitions.csv\"\n"));
    EXPECT_FALSE(Contains(s, "CONFIG_ESP32S3_DATA_CACHE_LINE_64B"));  // S3 only
}

TEST_F(ESPIDFBuildFilesTest, FlashSizeRoundsDownToWhatESPIDFOffers)
{
    PlatformConfig c = Board();
    c.SetOptionU32("flashSize", 16u * 1024 * 1024);
    EXPECT_TRUE(Contains(Sdkconfig(c), "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y"));
    c.SetOptionU32("flashSize", 12u * 1024 * 1024);
    EXPECT_TRUE(Contains(Sdkconfig(c), "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y"));
    c.SetOptionU32("flashSize", 0);
    EXPECT_TRUE(Contains(Sdkconfig(c), "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y"));
}

TEST_F(ESPIDFBuildFilesTest, PsramFollowsExternalMemory)
{
    PlatformConfig c = Board();
    EXPECT_FALSE(Contains(Sdkconfig(c), "CONFIG_SPIRAM=y"));
    c.externalMemorySize = 8u * 1024 * 1024;
    c.SetOption("psramMode", "oct");
    const std::string s = Sdkconfig(c);
    EXPECT_TRUE(Contains(s, "CONFIG_SPIRAM=y"));
    EXPECT_TRUE(Contains(s, "CONFIG_SPIRAM_MODE_OCT=y"));
}

TEST_F(ESPIDFBuildFilesTest, TheBoardsOwnLinesComeLastAndWin)
{
    PlatformConfig c = Board();
    c.SetOptionList("sdkconfig", { "CONFIG_FREERTOS_HZ=100" });
    const std::string s = Sdkconfig(c);
    const size_t generated = s.find("CONFIG_FREERTOS_HZ=1000");
    const size_t boards = s.find("CONFIG_FREERTOS_HZ=100\n");
    ASSERT_NE(generated, std::string::npos);
    ASSERT_NE(boards, std::string::npos);
    EXPECT_GT(boards, generated);
}

TEST_F(ESPIDFBuildFilesTest, TheDefaultPartitionTableFillsTheFlash)
{
    // The data partition starts where the 3 MB app ends and runs to the end
    // of the flash, on every size the sdkconfig can select.
    for (uint32_t mb : { 4u, 8u, 16u })
    {
        PlatformConfig c = Board();
        c.SetOptionU32("flashSize", mb * 1024 * 1024);
        const std::string csv = ReadFile(Generate(c) / "partitions.csv");
        EXPECT_TRUE(Contains(csv, "app0,     app,  ota_0,   0x10000,  0x300000,")) << csv;
        std::ostringstream data;
        data << "spiffs,   data, spiffs,  0x310000, 0x" << std::hex << (mb * 1024 * 1024 - 0x310000) << ",";
        EXPECT_TRUE(Contains(csv, data.str())) << mb << " MB:\n" << csv;
    }
}

TEST_F(ESPIDFBuildFilesTest, ABoardsOwnPartitionTableIsUsedAsIs)
{
    PlatformConfig c = Board();
    c.SetOption("partitionTable", "nvs, data, nvs, 0x9000, 0x6000,\n");
    EXPECT_EQ(ReadFile(Generate(c) / "partitions.csv"), "nvs, data, nvs, 0x9000, 0x6000,\n");
}

TEST_F(ESPIDFBuildFilesTest, AChangedDefaultRemovesTheStaleSdkconfig)
{
    // ESP-IDF ignores sdkconfig.defaults once an sdkconfig exists, so a
    // changed setting would never reach the build without this.
    PlatformConfig c = Board();
    Generate(c);
    const fs::path sdkconfig = fs::path(builder.GetBuildDirectory(m_Project.string())) / "sdkconfig";
    std::ofstream(sdkconfig) << "CONFIG_FROM_LAST_BUILD=y\n";

    Generate(c);  // nothing changed: the build's sdkconfig stays
    EXPECT_TRUE(fs::exists(sdkconfig));

    c.SetOptionU32("flashSize", 16u * 1024 * 1024);
    Generate(c);
    EXPECT_FALSE(fs::exists(sdkconfig));
}
