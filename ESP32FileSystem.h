#pragma once

#include <deki/FileImage.h>
#include <deki/providers/IFileSystem.h>
#include <string>

#ifdef ESP32
#include "esp_partition.h"
#endif

/// ESP32 file system. F:/ is the board's flash, made of two partitions:
/// - "gamedata": the game's data (the boot payload and, on internal storage,
///   its assets) as a read-only file image (Deki::FileImage), mapped into
///   memory, so opening a file is a binary search and reading it a copy out
///   of flash. Flashed with the firmware and never written.
/// - "userdata": what the game writes, on LittleFS, which no flashing touches.
/// An F:/ file is read from the game's data when it is there, else from the
/// user data; it is written to the user data, and never over a file of the
/// game's data. Other paths go to the ESP-IDF VFS with POSIX calls:
/// S:/file.bin -> /storage/file.bin.

namespace Deki
{

class ESP32FileSystem : public IFileSystem
{
public:
    static constexpr const char* kGameDataPartition = "gamedata";
    static constexpr const char* kUserDataPartition = "userdata";

private:
    bool m_Initialized;
    FileImage m_Image;
    // Where F:/ files the game wrote live: the user data partition's mount
    // point, or nothing when it could not be mounted.
    std::string m_UserRoot;
#ifdef ESP32
    esp_partition_mmap_handle_t m_MapHandle = 0;
    bool m_Mapped = false;
    bool m_UserDataMounted = false;
    bool m_MountTried = false;
#endif
    // Mounts the user data and maps the game's data, once, on first use.
    void MountStorage();

    // An open file of the game's data: where its bytes are and how far it has
    // been read.
    struct ImageFile
    {
        const uint8_t* data = nullptr;
        size_t size = 0;
        size_t position = 0;
        bool open = false;
    };
    // Files of the game's data open at once; a handle is a pointer into this.
    static constexpr int kMaxImageFiles = 16;
    ImageFile m_ImageFiles[kMaxImageFiles];

    ImageFile* AsImageFile(FileHandle handle);
    // The file of the game's data `path` names, or false when it is not an
    // F:/ path or the game's data has no such file.
    bool FindInImage(const char* path, const uint8_t*& data, size_t& size) const;

    // Virtual path to VFS path: "F:/save.bin" -> "/userdata/save.bin",
    // "S:/save.bin" -> "/storage/save.bin". Empty for an F:/ path with no
    // user data mounted.
    std::string ConvertPathInternal(const char* virtualPath);

public:
    ESP32FileSystem();
    virtual ~ESP32FileSystem();

    /// The partitions are mounted on first use.
    bool Initialize() override;
    void Shutdown() override;

    /// Serves the game's data from the image in `data`, which must stay where
    /// it is. MountStorage calls it with the mapped partition; tests hand it
    /// bytes. False when `data` is not a well-formed image.
    bool MountImage(const uint8_t* data, size_t size);

    /// Keeps the F:/ files the game writes in `directory`. MountStorage uses
    /// the user data partition's mount point; tests a folder.
    void UseUserDataDirectory(const std::string& directory);

    FileHandle OpenFile(const char* path, OpenMode mode) override;
    void CloseFile(FileHandle handle) override;
    size_t ReadFile(FileHandle handle, void* buffer, size_t size) override;
    size_t WriteFile(FileHandle handle, const void* buffer, size_t size) override;
    long SeekFile(FileHandle handle, long offset, SeekOrigin origin) override;
    long TellFile(FileHandle handle) override;
    long GetFileSize(FileHandle handle) override;
    bool FileExists(const char* path) override;
    bool ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize) override;
};

}  // namespace Deki
