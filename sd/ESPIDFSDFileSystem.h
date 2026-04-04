#pragma once

#include "providers/IDekiFileSystem.h"
#include <string>

// Forward declaration
class ESPIDFSDCard;

/**
 * @brief ESP-IDF native SD card implementation of IDekiFileSystem
 *
 * Uses POSIX file operations on the ESP-IDF VFS-mounted SD card.
 * After esp_vfs_fat_sdspi_mount(), the SD card is accessible via
 * standard fopen/fread/fwrite at the configured mount point.
 *
 * Path conversion:
 * - "S:/saves/game.sav" -> "/sdcard/saves/game.sav" (VFS mount point)
 * - "D:/saves/game.sav" -> "/sdcard/saves/game.sav" (legacy prefix)
 */
class ESPIDFSDFileSystem : public IDekiFileSystem
{
public:
    /**
     * @brief Construct filesystem wrapper
     * @param sdCard Parent SD card module (provides mount point)
     */
    explicit ESPIDFSDFileSystem(ESPIDFSDCard* sdCard);
    ~ESPIDFSDFileSystem() override;

    // IDekiFileSystem interface
    bool Initialize() override;
    void Shutdown() override;
    FileHandle OpenFile(const char* path, OpenMode mode) override;
    void CloseFile(FileHandle handle) override;
    size_t ReadFile(FileHandle handle, void* buffer, size_t size) override;
    size_t WriteFile(FileHandle handle, const void* buffer, size_t size) override;
    long SeekFile(FileHandle handle, long offset, SeekOrigin origin) override;
    long TellFile(FileHandle handle) override;
    long GetFileSize(FileHandle handle) override;
    bool FileExists(const char* path) override;
    bool ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize) override;

private:
    ESPIDFSDCard* m_SDCard;
    std::string m_MountPoint;

    // Convert virtual path (S:/... or D:/...) to VFS path (/sdcard/...)
    std::string ConvertVirtualPath(const char* virtualPath);
};
