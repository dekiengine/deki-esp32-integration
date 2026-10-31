#pragma once

#include <deki/providers/IMemoryProvider.h>

// ESP32 memory provider on the ESP-IDF heap_caps API, with PSRAM (SPI RAM)
// support for the ESP32, ESP32-S2, ESP32-S3 and others.

namespace Deki
{

#if defined(ESP32)

class ESP32MemoryProvider : public IMemoryProvider
{
public:
    ESP32MemoryProvider() = default;
    ~ESP32MemoryProvider() override = default;

    bool Initialize() override;
    void Shutdown() override;

    bool Serves(Memory::Region region) const override;
    void* Allocate(Memory::Region region, size_t bytes, bool needsDma) override;
    void Free(Memory::Region region, void* ptr) override;
    size_t GetAvailable(Memory::Region region) const override;

    // operator new, std::vector and std::string. Internal RAM while more than
    // kRawInternalHeadroom of it would stay free, PSRAM after that.
    void* AllocateRaw(size_t size) override;
    void FreeRaw(void* ptr) override;
    size_t GetRawBlockSize(void* ptr) const override;

private:
    /// PSRAM, cache-line aligned so a DMA engine and the CPU agree about it.
    void* AllocateExternalBytes(size_t size, bool needsDma);

private:
    bool m_HasPSRAM = false;
};

#endif  // ESP32

}  // namespace Deki
