#pragma once

#include <deki/providers/IFileSystem.h>
#include <string>

namespace DekiEsp32
{

class ESPIDFSDCard;

/// Deki::IFileSystem on the SD card ESP-IDF's VFS has mounted, with POSIX
/// file calls (fopen/fread/fwrite) at the mount point.
///
/// Path conversion:
/// - "S:/saves/game.sav" -> "/sdcard/saves/game.sav" (VFS mount point)
/// - "D:/saves/game.sav" -> "/sdcard/saves/game.sav" (legacy prefix)
class ESPIDFSDFileSystem : public Deki::IFileSystem
{
public:
    /// `sdCard` is the SD card package that owns this and gives the mount point.
    explicit ESPIDFSDFileSystem(ESPIDFSDCard* sdCard);
    ~ESPIDFSDFileSystem() override;

    // Deki::IFileSystem
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

    // Virtual path (S:/... or D:/...) to VFS path (/sdcard/...)
    std::string ConvertVirtualPath(const char* virtualPath);

    // Same into a caller's buffer, with no heap allocation
    bool ConvertVirtualPathTo(const char* virtualPath, char* outBuffer, size_t bufSize);
};

}  // namespace DekiEsp32
