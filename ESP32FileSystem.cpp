#include "ESP32FileSystem.h"
#include <cstring>

#ifdef ESP32
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include <cstdio>
#include <sys/stat.h>
#endif

namespace Deki
{

#ifdef ESP32

static const char* TAG = "ESP32FS";

// True when the partition's first two blocks (where LittleFS keeps its
// superblocks) were never written: erased flash reads as 0xFF.
static bool PartitionIsBlank(const char* label)
{
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!part)
    {
        return false;
    }
    uint8_t buf[256];
    const size_t span = part->size < 8192 ? part->size : 8192;
    for (size_t off = 0; off < span; off += sizeof(buf))
    {
        const size_t n = (span - off) < sizeof(buf) ? (span - off) : sizeof(buf);
        if (esp_partition_read(part, off, buf, n) != ESP_OK)
        {
            return false;
        }
        for (size_t i = 0; i < n; ++i)
        {
            if (buf[i] != 0xFF)
            {
                return false;
            }
        }
    }
    return true;
}

ESP32FileSystem::ESP32FileSystem()
    : m_Initialized(false)
{
}

ESP32FileSystem::~ESP32FileSystem()
{
    Shutdown();
}

bool ESP32FileSystem::Initialize()
{
    if (m_Initialized)
    {
        return true;
    }

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = "/littlefs";
    conf.partition_label = "spiffs";
    // Never format on a failed mount: the partition holds the boot payload,
    // the assets and whatever the game saved there, and formatting wiped all
    // of it on any fault. Only a partition never written is formatted.
    conf.format_if_mount_failed = false;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK && PartitionIsBlank("spiffs"))
    {
        ESP_LOGI(TAG, "LittleFS partition is blank; formatting it");
        if (esp_littlefs_format("spiffs") == ESP_OK)
        {
            ret = esp_vfs_littlefs_register(&conf);
        }
    }
    if (ret != ESP_OK)
    {
        // The engine runs on without F:/, so the error shows on the console
        // rather than the board stopping at boot. Flashing the firmware again
        // rewrites the partition.
        ESP_LOGE(TAG,
                 "LittleFS mount failed: %s. Running without F:/; nothing on it was changed. "
                 "Flash the firmware again to rewrite it.",
                 esp_err_to_name(ret));
        return true;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info("spiffs", &total, &used);
    ESP_LOGI(TAG, "LittleFS mounted: %zu/%zu bytes used", used, total);

    m_Initialized = true;
    return true;
}

void ESP32FileSystem::Shutdown()
{
    if (m_Initialized)
    {
        esp_vfs_littlefs_unregister("spiffs");
        m_Initialized = false;
    }
}

std::string ESP32FileSystem::ConvertPathInternal(const char* virtualPath)
{
    if (!virtualPath)
    {
        return "";
    }

    std::string path(virtualPath);

    // Convert "F:/..." to "/littlefs/..." (VFS mount point)
    if (path.length() >= 3 && path.substr(0, 3) == "F:/")
    {
        path = "/littlefs/" + path.substr(3);
    }
    else if (path.length() >= 3 && path.substr(0, 3) == "S:/")
    {
        path = "/storage/" + path.substr(3);
    }

    return path;
}

IFileSystem::FileHandle ESP32FileSystem::OpenFile(const char* path, OpenMode mode)
{
    if (!m_Initialized || !path)
    {
        return nullptr;
    }

    std::string real_path = ConvertPathInternal(path);
    const char* mode_str = "";

    switch (mode)
    {
        case OpenMode::READ_BINARY:
        case OpenMode::READ_TEXT: mode_str = "r"; break;
        case OpenMode::WRITE_BINARY:
        case OpenMode::WRITE_TEXT: mode_str = "w"; break;
    }

    FILE* f = fopen(real_path.c_str(), mode_str);
    if (!f)
    {
        return nullptr;
    }

    return static_cast<FileHandle>(f);
}

void ESP32FileSystem::CloseFile(FileHandle handle)
{
    if (handle)
    {
        fclose(static_cast<FILE*>(handle));
    }
}

size_t ESP32FileSystem::ReadFile(FileHandle handle, void* buffer, size_t size)
{
    if (!handle || !buffer)
    {
        return 0;
    }
    return fread(buffer, 1, size, static_cast<FILE*>(handle));
}

size_t ESP32FileSystem::WriteFile(FileHandle handle, const void* buffer, size_t size)
{
    if (!handle || !buffer)
    {
        return 0;
    }
    return fwrite(buffer, 1, size, static_cast<FILE*>(handle));
}

long ESP32FileSystem::SeekFile(FileHandle handle, long offset, SeekOrigin origin)
{
    if (!handle)
    {
        return -1;
    }

    int whence = SEEK_SET;
    switch (origin)
    {
        case SeekOrigin::BEGIN: whence = SEEK_SET; break;
        case SeekOrigin::CURRENT: whence = SEEK_CUR; break;
        case SeekOrigin::END: whence = SEEK_END; break;
    }

    if (fseek(static_cast<FILE*>(handle), offset, whence) != 0)
    {
        return -1;
    }

    return ftell(static_cast<FILE*>(handle));
}

long ESP32FileSystem::TellFile(FileHandle handle)
{
    if (!handle)
    {
        return -1;
    }
    return ftell(static_cast<FILE*>(handle));
}

long ESP32FileSystem::GetFileSize(FileHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    FILE* f = static_cast<FILE*>(handle);
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, cur, SEEK_SET);
    return size;
}

bool ESP32FileSystem::FileExists(const char* path)
{
    if (!m_Initialized || !path)
    {
        return false;
    }

    std::string real_path = ConvertPathInternal(path);
    struct stat st;
    return stat(real_path.c_str(), &st) == 0;
}

bool ESP32FileSystem::ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize)
{
    if (!virtualPath || !outBuffer || bufferSize == 0)
    {
        return false;
    }

    std::string converted = ConvertPathInternal(virtualPath);
    if (converted.length() >= bufferSize)
    {
        return false;
    }

    strcpy(outBuffer, converted.c_str());
    return true;
}

#else

// Non-ESP32 stub implementation
ESP32FileSystem::ESP32FileSystem()
    : m_Initialized(false)
{
}
ESP32FileSystem::~ESP32FileSystem()
{
}
bool ESP32FileSystem::Initialize()
{
    return false;
}
void ESP32FileSystem::Shutdown()
{
}
std::string ESP32FileSystem::ConvertPathInternal(const char*)
{
    return "";
}
IFileSystem::FileHandle ESP32FileSystem::OpenFile(const char*, OpenMode)
{
    return nullptr;
}
void ESP32FileSystem::CloseFile(FileHandle)
{
}
size_t ESP32FileSystem::ReadFile(FileHandle, void*, size_t)
{
    return 0;
}
size_t ESP32FileSystem::WriteFile(FileHandle, const void*, size_t)
{
    return 0;
}
long ESP32FileSystem::SeekFile(FileHandle, long, SeekOrigin)
{
    return -1;
}
long ESP32FileSystem::TellFile(FileHandle)
{
    return -1;
}
long ESP32FileSystem::GetFileSize(FileHandle)
{
    return -1;
}
bool ESP32FileSystem::FileExists(const char*)
{
    return false;
}
bool ESP32FileSystem::ConvertPath(const char*, char*, size_t)
{
    return false;
}

#endif

}  // namespace Deki
