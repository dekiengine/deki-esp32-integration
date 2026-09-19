#include "ESP32MemoryProvider.h"

#if defined(ESP32)
// ESP-IDF heap capabilities API
#include "esp_heap_caps.h"
#include "esp_system.h"
#endif

namespace Deki
{

#if defined(ESP32)


bool ESP32MemoryProvider::Initialize()
{
    // Check if PSRAM is available
    size_t psramSize = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    m_HasPSRAM = (psramSize > 0);
    return true;
}

void ESP32MemoryProvider::Shutdown()
{
    // Nothing to clean up
}

// PSRAM on the ESP32-S3 is cached, so a buffer a DMA engine will also read has
// to sit on a cache line or the two disagree. MALLOC_CAP_CACHE_ALIGNED asks
// the heap for exactly that alongside the DMA capability, in one call.
//
// This used to be esp_dma_malloc(ESP_DMA_MALLOC_FLAG_PSRAM) with a plain
// heap_caps_malloc(SPIRAM | DMA) behind it - a fallback that was NOT cache
// aligned, whatever its comment said. ESP-IDF 6 removed esp_dma_malloc and
// names this capability as its replacement.
void* ESP32MemoryProvider::AllocateExternalBytes(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_CACHE_ALIGNED);
}

bool ESP32MemoryProvider::Serves(Memory::Region region) const
{
    if (region == Deki::Memory::Internal)
        return true;
    if (region == Deki::Memory::External)
        return m_HasPSRAM;
    // Anything else — a region some package defined — this board does not
    // have. Saying so is what lets the allocation fail by name instead of
    // quietly landing somewhere it does not belong.
    return false;
}

void* ESP32MemoryProvider::Allocate(Memory::Region region, size_t bytes, bool needsDma)
{
    if (region == Deki::Memory::External)
    {
        if (!m_HasPSRAM)
            return nullptr;
        return AllocateExternalBytes(bytes);
    }

    if (region != Deki::Memory::Internal)
        return nullptr;  // a region this board does not serve

    // 16-byte alignment lets the QuadBlit dispatcher engage S3 PIE SIMD
    // kernels. heap_caps_aligned_alloc pairs with heap_caps_free, which is
    // what Free() below uses for either capability.
    //
    // MALLOC_CAP_DMA when the caller asked for reachability: internal RAM is
    // mostly DMA-reachable on this part but not all of it, and a display
    // peripheral reading a framebuffer that is not gets silent corruption
    // rather than an error.
    const uint32_t caps = needsDma ? (MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
                                   : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return heap_caps_aligned_alloc(16, bytes, caps);
}

void ESP32MemoryProvider::Free(Memory::Region region, void* ptr)
{
    if (!ptr)
        return;
    // Every capability this provider hands out comes from the heap_caps
    // allocator, and heap_caps_free takes any of them, so the region does not
    // change what has to happen here — it is checked rather than used.
    (void)region;
    heap_caps_free(ptr);
}

size_t ESP32MemoryProvider::GetAvailable(Memory::Region region) const
{
    if (region == Deki::Memory::Internal)
        return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (region == Deki::Memory::External)
        return m_HasPSRAM ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
    return 0;
}

size_t ESP32MemoryProvider::GetRawBlockSize(void* ptr) const
{
    // Reads the block header the heap already keeps, so the raw path can
    // account for itself without carrying a header of its own. Usable size
    // rather than requested size, which is the truer footprint anyway.
    return ptr ? heap_caps_get_allocated_size(ptr) : 0;
}

#endif  // ESP32

}  // namespace Deki
