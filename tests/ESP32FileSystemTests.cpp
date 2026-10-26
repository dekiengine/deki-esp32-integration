// F:/ on an ESP32: the game's data as a file image (Deki::FileImage) mapped
// out of flash, and what the game writes beside it. Here the image is bytes in
// memory, handed over as Initialize hands over the mapped partition, the user
// data a temporary folder, and both are used through the engine's file calls
// as every loader and game uses them.

#include <gtest/gtest.h>

#include "ESP32FileSystem.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Deki;

namespace
{

std::vector<uint8_t> Bytes(size_t size, uint8_t seed)
{
    std::vector<uint8_t> b(size);
    for (size_t i = 0; i < size; i++)
    {
        b[i] = static_cast<uint8_t>(seed * 31 + i);
    }
    return b;
}

class ESP32FileSystemTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::vector<FileImage::File> files = { { "boot.scene", Bytes(300, 1) },
                                               { "assets/5a7800eb", Bytes(5000, 2) },
                                               { "assets/empty", {} } };
        std::string error;
        ASSERT_TRUE(FileImage::Write(files, m_Image, error)) << error;
        ASSERT_TRUE(m_Fs.Initialize());
        ASSERT_TRUE(m_Fs.MountImage(m_Image.data(), m_Image.size()));
        m_UserData = std::filesystem::temp_directory_path() /
                     ("deki-esp32fs-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(m_UserData);
        std::filesystem::create_directories(m_UserData);
        m_Fs.UseUserDataDirectory(m_UserData.generic_string());
    }

    void TearDown() override
    {
        m_Fs.Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(m_UserData, ec);
    }

    bool Write(const char* path, const std::string& text)
    {
        IFileSystem::FileHandle f = m_Fs.OpenFile(path, IFileSystem::OpenMode::WriteBinary);
        if (!f)
        {
            return false;
        }
        const bool ok = m_Fs.WriteFile(f, text.data(), text.size()) == text.size();
        m_Fs.CloseFile(f);
        return ok;
    }

    std::string Read(const char* path)
    {
        IFileSystem::FileHandle f = m_Fs.OpenFile(path, IFileSystem::OpenMode::ReadBinary);
        if (!f)
        {
            return "<none>";
        }
        std::string text(static_cast<size_t>(m_Fs.GetFileSize(f)), '\0');
        m_Fs.ReadFile(f, text.data(), text.size());
        m_Fs.CloseFile(f);
        return text;
    }

    std::vector<uint8_t> m_Image;
    std::filesystem::path m_UserData;
    ESP32FileSystem m_Fs;
};

}  // namespace

TEST_F(ESP32FileSystemTest, ReadsAFileWhole)
{
    IFileSystem::FileHandle f = m_Fs.OpenFile("F:/assets/5a7800eb", IFileSystem::OpenMode::ReadBinary);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(m_Fs.GetFileSize(f), 5000);
    std::vector<uint8_t> read(6000, 0);
    EXPECT_EQ(m_Fs.ReadFile(f, read.data(), read.size()), 5000u);  // no further than its end
    read.resize(5000);
    EXPECT_EQ(read, Bytes(5000, 2));
    EXPECT_EQ(m_Fs.ReadFile(f, read.data(), 1), 0u);
    m_Fs.CloseFile(f);
}

TEST_F(ESP32FileSystemTest, SeeksAndTells)
{
    IFileSystem::FileHandle f = m_Fs.OpenFile("F:/boot.scene", IFileSystem::OpenMode::ReadBinary);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(m_Fs.SeekFile(f, 0, IFileSystem::SeekOrigin::End), 300);
    EXPECT_EQ(m_Fs.TellFile(f), 300);
    EXPECT_EQ(m_Fs.SeekFile(f, 10, IFileSystem::SeekOrigin::Begin), 10);
    EXPECT_EQ(m_Fs.SeekFile(f, 5, IFileSystem::SeekOrigin::Current), 15);
    uint8_t b = 0;
    EXPECT_EQ(m_Fs.ReadFile(f, &b, 1), 1u);
    EXPECT_EQ(b, Bytes(300, 1)[15]);
    EXPECT_EQ(m_Fs.SeekFile(f, 1, IFileSystem::SeekOrigin::End), -1);  // past the end
    EXPECT_EQ(m_Fs.SeekFile(f, -1, IFileSystem::SeekOrigin::Begin), -1);
    m_Fs.CloseFile(f);
}

TEST_F(ESP32FileSystemTest, KnowsWhatIsThere)
{
    EXPECT_TRUE(m_Fs.FileExists("F:/boot.scene"));
    EXPECT_TRUE(m_Fs.FileExists("F:/assets/empty"));
    EXPECT_FALSE(m_Fs.FileExists("F:/assets/missing"));
    EXPECT_FALSE(m_Fs.FileExists("F:/assets"));
    EXPECT_EQ(m_Fs.OpenFile("F:/assets/missing", IFileSystem::OpenMode::ReadBinary), nullptr);
}

TEST_F(ESP32FileSystemTest, TheGameWritesItsOwnFiles)
{
    ASSERT_TRUE(Write("F:/save.bin", "level 3"));
    EXPECT_TRUE(m_Fs.FileExists("F:/save.bin"));
    EXPECT_EQ(Read("F:/save.bin"), "level 3");
    ASSERT_TRUE(Write("F:/save.bin", "level 4"));  // and writes them again
    EXPECT_EQ(Read("F:/save.bin"), "level 4");
    EXPECT_TRUE(std::filesystem::exists(m_UserData / "save.bin"));
}

TEST_F(ESP32FileSystemTest, TheGamesDataIsNeverWritten)
{
    EXPECT_FALSE(Write("F:/boot.scene", "changed"));
    EXPECT_FALSE(Write("F:/assets/5a7800eb", "changed"));
    EXPECT_FALSE(std::filesystem::exists(m_UserData / "boot.scene"));
    IFileSystem::FileHandle f = m_Fs.OpenFile("F:/boot.scene", IFileSystem::OpenMode::ReadBinary);
    ASSERT_NE(f, nullptr);
    const uint8_t b = 0;
    EXPECT_EQ(m_Fs.WriteFile(f, &b, 1), 0u);
    m_Fs.CloseFile(f);
    EXPECT_EQ(Read("F:/boot.scene").size(), 300u);
}

TEST_F(ESP32FileSystemTest, TheGamesDataComesFirst)
{
    // A file of the same name left in the user data (an older layout) is not
    // what the game reads.
    std::ofstream(m_UserData / "boot.scene", std::ios::binary) << "stale";
    const std::vector<uint8_t> boot = Bytes(300, 1);
    EXPECT_EQ(Read("F:/boot.scene"), std::string(boot.begin(), boot.end()));
}

TEST_F(ESP32FileSystemTest, NoUserDataMeansReadOnly)
{
    m_Fs.UseUserDataDirectory("");
    EXPECT_FALSE(Write("F:/save.bin", "x"));
    EXPECT_FALSE(m_Fs.FileExists("F:/save.bin"));
    EXPECT_EQ(Read("F:/boot.scene").size(), 300u);
}

TEST_F(ESP32FileSystemTest, ManyOpenAtOnceAndClosedSlotsComeBack)
{
    std::vector<IFileSystem::FileHandle> open;
    for (;;)
    {
        IFileSystem::FileHandle f = m_Fs.OpenFile("F:/boot.scene", IFileSystem::OpenMode::ReadBinary);
        if (!f)
        {
            break;
        }
        open.push_back(f);
        ASSERT_LT(open.size(), 1000u);
    }
    EXPECT_GE(open.size(), 8u);
    m_Fs.CloseFile(open.back());
    open.pop_back();
    IFileSystem::FileHandle again = m_Fs.OpenFile("F:/assets/5a7800eb", IFileSystem::OpenMode::ReadBinary);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(m_Fs.GetFileSize(again), 5000);  // its own file, not the slot's last
    open.push_back(again);
    for (IFileSystem::FileHandle f : open)
    {
        m_Fs.CloseFile(f);
    }
}

TEST_F(ESP32FileSystemTest, OnlyWrittenFilesHavePaths)
{
    char path[512];
    EXPECT_FALSE(m_Fs.ConvertPath("F:/boot.scene", path, sizeof(path)));  // bytes in the image
    ASSERT_TRUE(m_Fs.ConvertPath("F:/save.bin", path, sizeof(path)));
    EXPECT_EQ(std::string(path), m_UserData.generic_string() + "/save.bin");
    ASSERT_TRUE(m_Fs.ConvertPath("S:/save.bin", path, sizeof(path)));
    EXPECT_STREQ(path, "/storage/save.bin");
}
