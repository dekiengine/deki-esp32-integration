#include "ESPIDFSDFileSystem.h"
#include "ESPIDFSDCard.h"
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

#if defined(ESP32)
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

bool ESPIDFSDFileSystem::ConvertVirtualPathTo(const char* virtualPath, char* outBuffer, size_t bufSize)
{
    if (!virtualPath || !outBuffer || bufSize == 0)
        return false;

    const char* suffix = virtualPath;
    bool hasPrefix = false;

    // Check S:/ or D:/ prefix
    if (virtualPath[0] && virtualPath[1] == ':' && virtualPath[2] == '/' &&
        (virtualPath[0] == 'S' || virtualPath[0] == 's' || virtualPath[0] == 'D' || virtualPath[0] == 'd'))
    {
        suffix = virtualPath + 3;
        hasPrefix = true;
    }

    if (hasPrefix)
    {
        int written = snprintf(outBuffer, bufSize, "%s/%s", m_MountPoint.c_str(), suffix);
        if (written < 0 || static_cast<size_t>(written) >= bufSize)
            return false;
    }
    else
    {
        size_t len = strlen(virtualPath);
        if (len + 1 > bufSize)
            return false;
        memcpy(outBuffer, virtualPath, len + 1);
    }

    return true;
}

bool ESPIDFSDFileSystem::ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize)
{
    return ConvertVirtualPathTo(virtualPath, outBuffer, bufferSize);
}

IDekiFileSystem::FileHandle ESPIDFSDFileSystem::OpenFile(const char* path, OpenMode mode)
{
    char realPath[128];
    if (!ConvertVirtualPathTo(path, realPath, sizeof(realPath)))
        return nullptr;

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

    FILE* file = std::fopen(realPath, modeStr);
    return file ? static_cast<FileHandle>(file) : nullptr;
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
    int fd = fileno(file);
    struct stat st;
    if (fstat(fd, &st) == 0)
        return st.st_size;
    return -1;
}

bool ESPIDFSDFileSystem::FileExists(const char* path)
{
    char realPath[128];
    if (!ConvertVirtualPathTo(path, realPath, sizeof(realPath)))
        return false;
    struct stat st;
    return stat(realPath, &st) == 0;
}
