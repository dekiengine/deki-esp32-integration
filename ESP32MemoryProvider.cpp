#include "ESP32MemoryProvider.h"

#if defined(ESP32)
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "soc/soc_caps.h"
#endif

namespace Deki
{

#if defined(ESP32)

bool ESP32MemoryProvider::Initialize()
{
    size_t psramSize = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    m_HasPSRAM = (psramSize > 0);
    return true;
}

void ESP32MemoryProvider::Shutdown()
{
    // Nothing to clean up
}

// PSRAM on the ESP32-S3 is cached, so a buffer a DMA engine also reads must
// sit on a cache line or the two disagree. MALLOC_CAP_CACHE_ALIGNED asks the
// heap for that together with the DMA capability, in one call. ESP-IDF 6
// names it as the replacement for esp_dma_malloc.
//
// Only where PSRAM can do DMA at all (the S2 and S3). The classic ESP32
// registers its PSRAM without MALLOC_CAP_DMA, so asking for it there fails
// every External allocation, on a board with megabytes free.
void* ESP32MemoryProvider::AllocateExternalBytes(size_t size, bool needsDma)
{
#if SOC_PSRAM_DMA_CAPABLE
    (void)needsDma;
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_CACHE_ALIGNED);
#else
    if (needsDma)
    {
        return nullptr;  // this chip's PSRAM is out of a DMA engine's reach
    }
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

bool ESP32MemoryProvider::Serves(Memory::Region region) const
{
    if (region == Deki::Memory::Internal)
    {
        return true;
    }
    if (region == Deki::Memory::External)
    {
        return m_HasPSRAM;
    }
    // Anything else (a region some package defined) this board does not
    // have. Saying so lets the allocation fail by name instead of landing
    // somewhere it does not belong.
    return false;
}

void* ESP32MemoryProvider::Allocate(Memory::Region region, size_t bytes, bool needsDma)
{
    if (region == Deki::Memory::External)
    {
        if (!m_HasPSRAM)
        {
            return nullptr;
        }
        return AllocateExternalBytes(bytes, needsDma);
    }

    if (region != Deki::Memory::Internal)
    {
        return nullptr;  // a region this board does not serve
    }

    // 16-byte alignment lets the QuadBlit dispatcher use the S3 PIE SIMD
    // kernels. heap_caps_aligned_alloc pairs with heap_caps_free, which
    // Free() below uses for either capability.
    //
    // MALLOC_CAP_DMA when the caller needs DMA: most internal RAM on this part
    // is DMA-reachable but not all, and a display peripheral reading a
    // framebuffer outside it corrupts the image without an error.
    const uint32_t caps = needsDma ? (MALLOC_CAP_DMA | MALLOC_CAP_8BIT) : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return heap_caps_aligned_alloc(16, bytes, caps);
}

void ESP32MemoryProvider::Free(Memory::Region region, void* ptr)
{
    if (!ptr)
    {
        return;
    }
    // Everything this provider hands out comes from the heap_caps allocator,
    // and heap_caps_free takes any of it, so the region changes nothing here.
    (void)region;
    heap_caps_free(ptr);
}

size_t ESP32MemoryProvider::GetAvailable(Memory::Region region) const
{
    if (region == Deki::Memory::Internal)
    {
        return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (region == Deki::Memory::External)
    {
        return m_HasPSRAM ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
    }
    return 0;
}

size_t ESP32MemoryProvider::GetRawBlockSize(void* ptr) const
{
    // Reads the block header the heap already keeps, so the raw path can
    // account for itself without a header of its own. The usable size, not
    // the requested one, which is the truer footprint anyway.
    return ptr ? heap_caps_get_allocated_size(ptr) : 0;
}

#endif  // ESP32

}  // namespace Deki
