#include "ESPIDFSDFileSystem.h"
#include "ESPIDFSDCard.h"
#include <cstring>
#include <cstdio>

#if defined(ESP32)
#include <sys/stat.h>
#include "esp_log.h"

static const char* TAG = "ESPIDFSDFS";
#endif

ESPIDFSDFileSystem::ESPIDFSDFileSystem(ESPIDFSDCard* sdCard)
    : m_SDCard(sdCard)
{
    if (m_SDCard)
    {
        m_MountPoint = m_SDCard->GetMountPoint();
    }
}

ESPIDFSDFileSystem::~ESPIDFSDFileSystem()
{
    Shutdown();
}

bool ESPIDFSDFileSystem::Initialize()
{
    // Initialization is handled by ESPIDFSDCard
    return true;
}

void ESPIDFSDFileSystem::Shutdown()
{
    // Shutdown is handled by ESPIDFSDCard
}

std::string ESPIDFSDFileSystem::ConvertVirtualPath(const char* virtualPath)
{
    if (!virtualPath)
        return "";

    std::string path(virtualPath);

    // Convert "S:/" prefix to VFS mount point path
    // ESP-IDF VFS uses full mount point paths (e.g., "/sdcard/prefabs/Demo")
    if (path.length() >= 3 && (path[0] == 'S' || path[0] == 's') && path[1] == ':' && path[2] == '/')
    {
        path = m_MountPoint + "/" + path.substr(3);
    }
    // Legacy: Convert "D:/" prefix
    else if (path.length() >= 3 && (path[0] == 'D' || path[0] == 'd') && path[1] == ':' && path[2] == '/')
    {
        path = m_MountPoint + "/" + path.substr(3);
    }

    // Ensure forward slashes
    for (char& c : path)
    {
        if (c == '\\')
            c = '/';
    }

    return path;
}

bool ESPIDFSDFileSystem::ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize)
{
    std::string converted = ConvertVirtualPath(virtualPath);

    if (converted.length() + 1 > bufferSize)
        return false;

    std::strncpy(outBuffer, converted.c_str(), bufferSize);
    outBuffer[bufferSize - 1] = '\0';
    return true;
}

IDekiFileSystem::FileHandle ESPIDFSDFileSystem::OpenFile(const char* path, OpenMode mode)
{
    std::string realPath = ConvertVirtualPath(path);

    // Common extensions to try if file not found (for extension-less loading)
    static const char* extensions[] = { "", ".prefab", ".png", ".jpg", ".tex", ".dfont", ".frameanim", ".wav", ".mp3", nullptr };

    const char* modeStr;
    switch (mode)
    {
        case OpenMode::READ_BINARY:
            modeStr = "rb";
            break;
        case OpenMode::WRITE_BINARY:
            modeStr = "wb";
            break;
        case OpenMode::READ_TEXT:
            modeStr = "r";
            break;
        case OpenMode::WRITE_TEXT:
            modeStr = "w";
            break;
        default:
            return nullptr;
    }

    // Try exact path first, then with extensions
    for (const char** ext = extensions; *ext != nullptr; ++ext)
    {
        std::string tryPath = realPath + *ext;
        FILE* file = std::fopen(tryPath.c_str(), modeStr);
        if (file)
            return static_cast<FileHandle>(file);
    }

    return nullptr;
}

void ESPIDFSDFileSystem::CloseFile(FileHandle handle)
{
    if (!handle)
        return;

    FILE* file = static_cast<FILE*>(handle);
    std::fclose(file);
}

size_t ESPIDFSDFileSystem::ReadFile(FileHandle handle, void* buffer, size_t size)
{
    if (!handle || !buffer)
        return 0;

    FILE* file = static_cast<FILE*>(handle);
    return std::fread(buffer, 1, size, file);
}

size_t ESPIDFSDFileSystem::WriteFile(FileHandle handle, const void* buffer, size_t size)
{
    if (!handle || !buffer)
        return 0;

    FILE* file = static_cast<FILE*>(handle);
    return std::fwrite(buffer, 1, size, file);
}

long ESPIDFSDFileSystem::SeekFile(FileHandle handle, long offset, SeekOrigin origin)
{
    if (!handle)
        return -1;

    FILE* file = static_cast<FILE*>(handle);
    int whence;
    switch (origin)
    {
        case SeekOrigin::BEGIN:
            whence = SEEK_SET;
            break;
        case SeekOrigin::CURRENT:
            whence = SEEK_CUR;
            break;
        case SeekOrigin::END:
            whence = SEEK_END;
            break;
        default:
            return -1;
    }

    if (std::fseek(file, offset, whence) != 0)
        return -1;

    return std::ftell(file);
}

long ESPIDFSDFileSystem::TellFile(FileHandle handle)
{
    if (!handle)
        return -1;

    FILE* file = static_cast<FILE*>(handle);
    return std::ftell(file);
}

long ESPIDFSDFileSystem::GetFileSize(FileHandle handle)
{
    if (!handle)
        return -1;

    FILE* file = static_cast<FILE*>(handle);
    long currentPos = std::ftell(file);
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, currentPos, SEEK_SET);
    return size;
}

bool ESPIDFSDFileSystem::FileExists(const char* path)
{
    std::string realPath = ConvertVirtualPath(path);

    // Common extensions to try if file not found
    static const char* extensions[] = { "", ".prefab", ".png", ".jpg", ".tex", ".dfont", ".frameanim", ".wav", ".mp3", nullptr };

#if defined(ESP32)
    for (const char** ext = extensions; *ext != nullptr; ++ext)
    {
        std::string tryPath = realPath + *ext;
        struct stat st;
        if (stat(tryPath.c_str(), &st) == 0)
            return true;
    }
    return false;
#else
    for (const char** ext = extensions; *ext != nullptr; ++ext)
    {
        std::string tryPath = realPath + *ext;
        FILE* file = std::fopen(tryPath.c_str(), "r");
        if (file)
        {
            std::fclose(file);
            return true;
        }
    }
    return false;
#endif
}
