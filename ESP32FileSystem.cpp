#include "ESP32FileSystem.h"
#include <deki/LogSystem.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#ifdef ESP32
#include "esp_littlefs.h"
#include "esp_log.h"
#endif

namespace Deki
{

namespace
{

constexpr char kFlashPrefix[] = "F:/";
constexpr size_t kFlashPrefixLength = sizeof(kFlashPrefix) - 1;

bool IsFlashPath(const char* path)
{
    return path && std::strncmp(path, kFlashPrefix, kFlashPrefixLength) == 0;
}

#ifdef ESP32
const char* TAG = "ESP32FS";

// Where the user data partition is mounted in the VFS.
constexpr char kUserDataMount[] = "/userdata";

// True when the partition has held a LittleFS: its superblock, in one of the
// first two blocks, names it. Erased flash, or bytes another layout left
// there, does not.
bool PartitionHoldsLittleFs(const esp_partition_t* part)
{
    static const char kName[] = "littlefs";
    char buf[257];
    const size_t span = part->size < 8192 ? part->size : 8192;
    // Overlapping reads, so the name is found across a read's edge.
    for (size_t off = 0; off < span; off += 256)
    {
        const size_t n = (span - off) < 256 ? (span - off) : 256;
        const size_t take = n + (off + n < span ? sizeof(kName) - 1 : 0);
        if (esp_partition_read(part, off, buf, take) != ESP_OK)
        {
            return true;  // unknown: never format what might be the game's
        }
        for (size_t i = 0; i + sizeof(kName) - 1 <= take; ++i)
        {
            if (std::memcmp(buf + i, kName, sizeof(kName) - 1) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

// Mounts what the game writes. Formats the partition only when it has never
// held a LittleFS (a new board, or one flashed with an older layout): one that
// holds a damaged filesystem holds the game's saves, and is left alone.
bool MountUserData()
{
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                                           ESP32FileSystem::kUserDataPartition);
    if (!part)
    {
        ESP_LOGW(TAG, "No '%s' partition: F:/ can be read but not written.", ESP32FileSystem::kUserDataPartition);
        return false;
    }
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = kUserDataMount;
    conf.partition_label = ESP32FileSystem::kUserDataPartition;
    conf.format_if_mount_failed = false;
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK && !PartitionHoldsLittleFs(part))
    {
        ESP_LOGI(TAG, "Preparing the '%s' partition for what the game writes", ESP32FileSystem::kUserDataPartition);
        if (esp_littlefs_format(ESP32FileSystem::kUserDataPartition) == ESP_OK)
        {
            ret = esp_vfs_littlefs_register(&conf);
        }
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "Cannot mount the '%s' partition: %s. F:/ can be read but not written; what the game wrote there "
                 "is left as it is.",
                 ESP32FileSystem::kUserDataPartition, esp_err_to_name(ret));
        return false;
    }
    return true;
}
#endif

}  // namespace

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
    // The partitions are mounted on first use (MountStorage): the file system
    // is installed by a static constructor, before app_main, where nothing it
    // logs reaches the console, errors included.
    m_Initialized = true;
    return true;
}

void ESP32FileSystem::MountStorage()
{
#ifdef ESP32
    if (m_MountTried)
    {
        return;
    }
    m_MountTried = true;

    if (MountUserData())
    {
        m_UserDataMounted = true;
        m_UserRoot = kUserDataMount;
    }

    // The engine runs on without the game's data when the image is missing
    // or damaged, so the error shows on the console instead of the board
    // stopping at boot. Flashing the firmware again writes it.
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, kGameDataPartition);
    if (!part)
    {
        ESP_LOGE(TAG, "No '%s' partition: the game's data is missing. Build and flash the firmware again.",
                 kGameDataPartition);
        return;
    }
    uint8_t header[FileImage::kHeaderSize];
    const uint32_t size = esp_partition_read(part, 0, header, sizeof(header)) == ESP_OK
                              ? FileImage::SizeFromHeader(header, sizeof(header))
                              : 0;
    if (size == 0 || size > part->size)
    {
        ESP_LOGE(TAG, "The game's data is not in the '%s' partition. Flash the firmware again.", kGameDataPartition);
        return;
    }
    // Only what the image takes, not the whole partition: the address space
    // flash is mapped into is shared with external RAM.
    const void* mapped = nullptr;
    const esp_err_t err = esp_partition_mmap(part, 0, size, ESP_PARTITION_MMAP_DATA, &mapped, &m_MapHandle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Cannot map the game's data (%u bytes): %s", static_cast<unsigned>(size), esp_err_to_name(err));
        return;
    }
    m_Mapped = true;
    if (!MountImage(static_cast<const uint8_t*>(mapped), size))
    {
        ESP_LOGE(TAG, "The game's data in the '%s' partition is damaged. Flash the firmware again.",
                 kGameDataPartition);
        return;
    }
    ESP_LOGI(TAG, "Game data: %u files, %u bytes, in flash%s", static_cast<unsigned>(m_Image.GetFileCount()),
             static_cast<unsigned>(size), m_UserDataMounted ? "; the game writes to the user data" : "");
#endif
}

bool ESP32FileSystem::MountImage(const uint8_t* data, size_t size)
{
    return m_Image.Open(data, size);
}

void ESP32FileSystem::UseUserDataDirectory(const std::string& directory)
{
    m_UserRoot = directory;
}

void ESP32FileSystem::Shutdown()
{
    if (!m_Initialized)
    {
        return;
    }
    for (ImageFile& f : m_ImageFiles)
    {
        f = ImageFile{};
    }
    m_Image = FileImage{};
    m_UserRoot.clear();
#ifdef ESP32
    m_MountTried = false;
    if (m_Mapped)
    {
        esp_partition_munmap(m_MapHandle);
        m_Mapped = false;
    }
    if (m_UserDataMounted)
    {
        esp_vfs_littlefs_unregister(kUserDataPartition);
        m_UserDataMounted = false;
    }
#endif
    m_Initialized = false;
}

std::string ESP32FileSystem::ConvertPathInternal(const char* virtualPath)
{
    if (!virtualPath)
    {
        return "";
    }

    std::string path(virtualPath);
    if (IsFlashPath(virtualPath))
    {
        // An F:/ file the game wrote: in the user data.
        return m_UserRoot.empty() ? std::string() : m_UserRoot + "/" + path.substr(kFlashPrefixLength);
    }
    // "S:/..." to "/storage/..." (the SD card's VFS mount point)
    if (path.length() >= 3 && path.substr(0, 3) == "S:/")
    {
        path = "/storage/" + path.substr(3);
    }
    return path;
}

bool ESP32FileSystem::FindInImage(const char* path, const uint8_t*& data, size_t& size) const
{
    if (!IsFlashPath(path))
    {
        return false;
    }
    const char* name = path + kFlashPrefixLength;
    return m_Image.Find(name, std::strlen(name), data, size);
}

ESP32FileSystem::ImageFile* ESP32FileSystem::AsImageFile(FileHandle handle)
{
    for (ImageFile& f : m_ImageFiles)
    {
        if (handle == &f)
        {
            return f.open ? &f : nullptr;
        }
    }
    return nullptr;
}

IFileSystem::FileHandle ESP32FileSystem::OpenFile(const char* path, OpenMode mode)
{
    MountStorage();
    if (!m_Initialized || !path)
    {
        return nullptr;
    }

    const bool reading = mode == OpenMode::ReadBinary || mode == OpenMode::ReadText;
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (FindInImage(path, data, size))
    {
        if (!reading)
        {
            // The game's data is what was flashed, the same every boot.
            DEKI_LOG_WARNING("%s is part of the game's data, which is never written; write the game's own files "
                             "under another name",
                             path);
            return nullptr;
        }
        for (ImageFile& f : m_ImageFiles)
        {
            if (!f.open)
            {
                f = ImageFile{ data, size, 0, true };
                return static_cast<FileHandle>(&f);
            }
        }
        DEKI_LOG_ERROR("More than %d files of the game's data open at once; %s was not opened", kMaxImageFiles, path);
        return nullptr;
    }

    const std::string realPath = ConvertPathInternal(path);
    if (realPath.empty())
    {
        return nullptr;
    }
    FILE* f = std::fopen(realPath.c_str(), reading ? "r" : "w");
    return static_cast<FileHandle>(f);
}

void ESP32FileSystem::CloseFile(FileHandle handle)
{
    if (ImageFile* f = AsImageFile(handle))
    {
        *f = ImageFile{};
        return;
    }
    if (handle)
    {
        std::fclose(static_cast<FILE*>(handle));
    }
}

size_t ESP32FileSystem::ReadFile(FileHandle handle, void* buffer, size_t size)
{
    if (!handle || !buffer)
    {
        return 0;
    }
    if (ImageFile* f = AsImageFile(handle))
    {
        const size_t n = size < f->size - f->position ? size : f->size - f->position;
        std::memcpy(buffer, f->data + f->position, n);
        f->position += n;
        return n;
    }
    return std::fread(buffer, 1, size, static_cast<FILE*>(handle));
}

size_t ESP32FileSystem::WriteFile(FileHandle handle, const void* buffer, size_t size)
{
    if (!handle || !buffer || AsImageFile(handle))
    {
        return 0;
    }
    return std::fwrite(buffer, 1, size, static_cast<FILE*>(handle));
}

long ESP32FileSystem::SeekFile(FileHandle handle, long offset, SeekOrigin origin)
{
    if (!handle)
    {
        return -1;
    }

    if (ImageFile* f = AsImageFile(handle))
    {
        long base = 0;
        switch (origin)
        {
            case SeekOrigin::Begin: base = 0; break;
            case SeekOrigin::Current: base = static_cast<long>(f->position); break;
            case SeekOrigin::End: base = static_cast<long>(f->size); break;
        }
        const long target = base + offset;
        if (target < 0 || target > static_cast<long>(f->size))
        {
            return -1;
        }
        f->position = static_cast<size_t>(target);
        return target;
    }

    int whence = SEEK_SET;
    switch (origin)
    {
        case SeekOrigin::Begin: whence = SEEK_SET; break;
        case SeekOrigin::Current: whence = SEEK_CUR; break;
        case SeekOrigin::End: whence = SEEK_END; break;
    }
    if (std::fseek(static_cast<FILE*>(handle), offset, whence) != 0)
    {
        return -1;
    }
    return std::ftell(static_cast<FILE*>(handle));
}

long ESP32FileSystem::TellFile(FileHandle handle)
{
    if (!handle)
    {
        return -1;
    }
    if (ImageFile* f = AsImageFile(handle))
    {
        return static_cast<long>(f->position);
    }
    return std::ftell(static_cast<FILE*>(handle));
}

long ESP32FileSystem::GetFileSize(FileHandle handle)
{
    if (!handle)
    {
        return -1;
    }
    if (ImageFile* f = AsImageFile(handle))
    {
        return static_cast<long>(f->size);
    }

    FILE* f = static_cast<FILE*>(handle);
    const long cur = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, cur, SEEK_SET);
    return size;
}

bool ESP32FileSystem::FileExists(const char* path)
{
    MountStorage();
    if (!m_Initialized || !path)
    {
        return false;
    }
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (FindInImage(path, data, size))
    {
        return true;
    }
    const std::string realPath = ConvertPathInternal(path);
    struct stat st;
    return !realPath.empty() && stat(realPath.c_str(), &st) == 0;
}

bool ESP32FileSystem::ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize)
{
    MountStorage();
    // A file of the game's data is bytes in the image, with no path of its
    // own.
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (!virtualPath || !outBuffer || bufferSize == 0 || FindInImage(virtualPath, data, size))
    {
        return false;
    }

    const std::string converted = ConvertPathInternal(virtualPath);
    if (converted.empty() || converted.length() >= bufferSize)
    {
        return false;
    }
    std::strcpy(outBuffer, converted.c_str());
    return true;
}

}  // namespace Deki
