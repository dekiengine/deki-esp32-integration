// Large copies by the ESP32-P4's AXI DMA. See P4DmaCopy.h.
//
// Why: the CPU writes PSRAM through its cache, which reads each line in
// before it is overwritten, so a CPU copy moves three times its size; the
// DMA moves twice its size. Measured on a P4 (chip v1.3, PSRAM at 200 MHz)
// with a 1024x600 panel streaming: a 1.2 MB PSRAM-to-PSRAM copy takes
// 15.9 ms on the CPU (one core or two) and 8.5 ms by DMA with 128-byte bursts.
//
// It drives the channels through ESP-IDF's esp_private GDMA API, which its
// own async memcpy is built on: esp_async_memcpy takes one contiguous source,
// and a copy out of a larger picture has a row stride.

#include "P4DmaCopy.h"

#if defined(ESP32)
#include <sdkconfig.h>  // CONFIG_IDF_TARGET_*
#endif

#if defined(ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <deki/LogSystem.h>

#include <esp_cache.h>
#include <esp_err.h>
#include <esp_memory_utils.h>
#include <esp_private/gdma.h>
#include <esp_private/gdma_link.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <climits>
#include <cstring>
#include <vector>

namespace DekiEsp32::Blit
{

namespace
{

constexpr size_t kMaxDescriptorBytes = 4095;  // what one descriptor carries
constexpr size_t kDescriptorAlign = 8;       // GDMA_LL_AXI_DESC_ALIGNMENT on the P4
constexpr uint32_t kBurstBytes = 128;        // measured faster than 64 (145 against 111 MB/s)
// Running, the DMA reports every kChunkRows rows: an EOF on each such row's
// descriptor.
constexpr int32_t kChunkRows = 32;
// A copy whose source and destination rows both follow on one another is one
// block, cut into pieces this size (whole bursts) rather than a descriptor a
// row: a strip of 28 rows of 2 KB takes 15 instead of 28 each way, which is
// most of the time it takes to set the copy up.
constexpr size_t kBlockPiece = (kMaxDescriptorBytes / kBurstBytes) * kBurstBytes;

bool Reachable(const void* p)
{
    return esp_ptr_external_ram(p) || esp_ptr_dma_capable(p);
}

bool IRAM_ATTR OnCopied(gdma_channel_handle_t, gdma_event_data_t*, void* engine)
{
    static_cast<DmaEngine*>(engine)->OnReport();
    return false;  // the waiter is woken through its semaphore below
}

}  // namespace

struct DmaEngine::State
{
    gdma_channel_handle_t tx = nullptr;
    gdma_channel_handle_t rx = nullptr;
    gdma_link_list_handle_t txList = nullptr;
    gdma_link_list_handle_t rxList = nullptr;
    int32_t listRows = 0;
    SemaphoreHandle_t done = nullptr;
    std::vector<gdma_buffer_mount_config_t> txMounts;
    std::vector<gdma_buffer_mount_config_t> rxMounts;
    // The copy running: what the CPU finishes if it stalls.
    const uint8_t* src = nullptr;
    size_t srcStride = 0;
    uint8_t* dst = nullptr;
    size_t dstStride = 0;
    size_t rowBytes = 0;
    int32_t height = 0;
};

void IRAM_ATTR DmaEngine::OnReport()
{
    m_Reports = m_Reports + 1;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(m_State->done, &woken);
    if (woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

bool DmaEngine::EnsureChannels()
{
    if (m_State && m_State->tx)
    {
        return true;
    }
    if (m_Failed)
    {
        return false;
    }
    auto fail = [this](const char* what, esp_err_t err)
    {
        m_Failed = true;
        DEKI_LOG_WARNING("DmaCopy (%s): %s (%s); its copies stay on the CPU", m_Name, what, esp_err_to_name(err));
        return false;
    };
    if (!m_State)
    {
        m_State = new State();
    }
    State& s = *m_State;
    gdma_channel_alloc_config_t alloc = {};
    esp_err_t err = gdma_new_axi_channel(&alloc, &s.tx, &s.rx);
    if (err != ESP_OK)
    {
        s.tx = s.rx = nullptr;
        return fail("no AXI DMA channel", err);
    }
    gdma_reset(s.tx);
    gdma_reset(s.rx);
    gdma_trigger_t m2m = GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_M2M, 0);
    uint32_t freeIds = 0;
    gdma_get_free_m2m_trig_id_mask(s.tx, &freeIds);
    if (freeIds == 0)
    {
        return fail("no memory-to-memory DMA trigger free", ESP_ERR_NOT_FOUND);
    }
    m2m.instance_id = __builtin_ctz(freeIds);
    if ((err = gdma_connect(s.rx, m2m)) != ESP_OK || (err = gdma_connect(s.tx, m2m)) != ESP_OK)
    {
        return fail("the DMA channels did not connect", err);
    }
    gdma_strategy_config_t strategy = {};
    strategy.owner_check = true;
    strategy.auto_update_desc = true;
    strategy.eof_till_data_popped = true;
    gdma_apply_strategy(s.tx, &strategy);
    gdma_apply_strategy(s.rx, &strategy);
    gdma_transfer_config_t transfer = {};
    transfer.max_data_burst_size = kBurstBytes;
    transfer.access_ext_mem = true;
    if ((err = gdma_config_transfer(s.tx, &transfer)) != ESP_OK || (err = gdma_config_transfer(s.rx, &transfer)) != ESP_OK)
    {
        return fail("the DMA transfer could not be set up", err);
    }
    s.done = xSemaphoreCreateBinary();
    gdma_rx_event_callbacks_t callbacks = {};
    callbacks.on_recv_eof = &OnCopied;
    if (!s.done || (err = gdma_register_rx_event_callbacks(s.rx, &callbacks, this)) != ESP_OK)
    {
        return fail("the DMA's completion could not be heard", err);
    }
    DEKI_LOG_INFO("DmaCopy (%s): copies by DMA", m_Name);
    return true;
}

bool DmaEngine::EnsureLists(int32_t rows)
{
    State& s = *m_State;
    if (rows <= s.listRows)
    {
        return true;
    }
    if (s.txList)
    {
        gdma_del_link_list(s.txList);
        s.txList = nullptr;
    }
    if (s.rxList)
    {
        gdma_del_link_list(s.rxList);
        s.rxList = nullptr;
    }
    s.listRows = 0;
    gdma_link_list_config_t config = {};
    config.num_items = static_cast<uint32_t>(rows);
    config.item_alignment = kDescriptorAlign;
    config.flags.check_owner = false;  // mounted again for every copy, after the last finished
    if (gdma_new_link_list(&config, &s.txList) != ESP_OK || gdma_new_link_list(&config, &s.rxList) != ESP_OK)
    {
        return false;  // no internal RAM for them now: this copy on the CPU
    }
    s.listRows = rows;
    s.txMounts.resize(static_cast<size_t>(rows));
    s.rxMounts.resize(static_cast<size_t>(rows));
    return true;
}

int32_t DmaEngine::RowsDone() const
{
    if (m_Whole)
    {
        return m_Reports > 0 ? m_State->height : 0;
    }
    const int32_t rows = m_Reports * kChunkRows;
    return rows < m_State->height ? rows : m_State->height;
}

void DmaEngine::Wait(int32_t rows)
{
    if (!m_Running)
    {
        return;
    }
    State& s = *m_State;
    const int32_t want = rows < s.height ? rows : s.height;
    while (RowsDone() < want)
    {
        if (xSemaphoreTake(s.done, pdMS_TO_TICKS(100)) != pdTRUE && RowsDone() < want)
        {
            // Stalled: stopped, and the rest copied on the CPU.
            gdma_reset(s.tx);
            gdma_reset(s.rx);
            for (int32_t y = RowsDone(); y < s.height; ++y)
            {
                std::memcpy(s.dst + static_cast<size_t>(y) * s.dstStride, s.src + static_cast<size_t>(y) * s.srcStride,
                            s.rowBytes);
            }
            m_Running = false;
            if (!m_SaidTimeout)
            {
                m_SaidTimeout = true;
                DEKI_LOG_WARNING("DmaCopy (%s): a copy stalled for 100 ms; its rows were copied on the CPU", m_Name);
            }
            return;
        }
    }
    if (RowsDone() >= s.height)
    {
        m_Running = false;
    }
}

QuadBlit::RectCopyResult DmaEngine::Copy(const uint8_t* src, size_t srcStride, uint8_t* dst, size_t dstStride,
                                         size_t rowBytes, int32_t height, bool mayRun)
{
    using QuadBlit::RectCopyResult;
    Wait(INT32_MAX);  // one at a time
    if (height <= 0 || rowBytes == 0 || rowBytes > kMaxDescriptorBytes || !Reachable(src) || !Reachable(dst))
    {
        return RectCopyResult::Refused;
    }
    const bool whole = srcStride == rowBytes && dstStride == rowBytes;
    const size_t total = rowBytes * static_cast<size_t>(height);
    const int32_t items = whole ? static_cast<int32_t>((total + kBlockPiece - 1) / kBlockPiece) : height;
    if (!EnsureChannels() || !EnsureLists(items))
    {
        return RectCopyResult::Refused;
    }
    State& s = *m_State;

    const size_t dstLine = esp_cache_get_line_size_by_addr(dst);
    const size_t dstSpan = static_cast<size_t>(height - 1) * dstStride + rowBytes;
    // Left running only over whole rows on whole cache lines: then nothing
    // the CPU writes meanwhile shares a line with a row still on its way.
    const bool running =
        mayRun && dstStride == rowBytes &&
        (dstLine == 0 || ((reinterpret_cast<uintptr_t>(dst) % dstLine) == 0 && dstSpan % dstLine == 0));
    m_Whole = whole;
    for (int32_t y = 0; whole && y < items; ++y)
    {
        // One block: its pieces, an EOF on the last only.
        const bool last = y == items - 1;
        const size_t at = static_cast<size_t>(y) * kBlockPiece;
        const size_t length = last ? total - at : kBlockPiece;
        gdma_buffer_mount_config_t& tx = s.txMounts[static_cast<size_t>(y)];
        tx = {};
        tx.buffer = const_cast<uint8_t*>(src + at);
        tx.buffer_alignment = 1;
        tx.length = length;
        tx.flags.mark_eof = last;
        tx.flags.mark_final = last ? GDMA_FINAL_LINK_TO_NULL : GDMA_FINAL_LINK_TO_DEFAULT;
        gdma_buffer_mount_config_t& rx = s.rxMounts[static_cast<size_t>(y)];
        rx = {};
        rx.buffer = dst + at;
        rx.buffer_alignment = 1;
        rx.length = length;
        rx.flags.mark_final = last ? GDMA_FINAL_LINK_TO_NULL : GDMA_FINAL_LINK_TO_DEFAULT;
    }
    for (int32_t y = 0; !whole && y < height; ++y)
    {
        const bool last = y == height - 1;
        const bool report = last || (running && (y + 1) % kChunkRows == 0);
        gdma_buffer_mount_config_t& tx = s.txMounts[static_cast<size_t>(y)];
        tx = {};
        tx.buffer = const_cast<uint8_t*>(src + static_cast<size_t>(y) * srcStride);
        tx.buffer_alignment = 1;
        tx.length = rowBytes;
        tx.flags.mark_eof = report;
        tx.flags.mark_final = last ? GDMA_FINAL_LINK_TO_NULL : GDMA_FINAL_LINK_TO_DEFAULT;
        gdma_buffer_mount_config_t& rx = s.rxMounts[static_cast<size_t>(y)];
        rx = {};
        rx.buffer = dst + static_cast<size_t>(y) * dstStride;
        rx.buffer_alignment = 1;
        rx.length = rowBytes;
        rx.flags.mark_final = last ? GDMA_FINAL_LINK_TO_NULL : GDMA_FINAL_LINK_TO_DEFAULT;
    }
    gdma_link_mount_buffers(s.txList, 0, s.txMounts.data(), static_cast<size_t>(items), nullptr);
    gdma_link_mount_buffers(s.rxList, 0, s.rxMounts.data(), static_cast<size_t>(items), nullptr);

    // What the CPU wrote to the source, in memory before the DMA reads it.
    const size_t srcSpan = static_cast<size_t>(height - 1) * srcStride + rowBytes;
    if (esp_cache_get_line_size_by_addr(src) > 0)
    {
        esp_cache_msync(const_cast<uint8_t*>(src), srcSpan, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }

    if (running)
    {
        // Every byte of these lines is overwritten: dropped, not written
        // back, so the CPU reads each row from memory once it has arrived.
        if (dstLine > 0)
        {
            esp_cache_msync(dst, dstSpan, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        }
        s.src = src;
        s.srcStride = srcStride;
        s.dst = dst;
        s.dstStride = dstStride;
        s.rowBytes = rowBytes;
        s.height = height;
        m_Reports = 0;
        m_Running = true;
        xSemaphoreTake(s.done, 0);
        gdma_start(s.rx, gdma_link_get_head_addr(s.rxList));
        gdma_start(s.tx, gdma_link_get_head_addr(s.txList));
        return RectCopyResult::Running;
    }

    // Waited for: the destination's rows written back first, so the lines
    // its edges share with pixels beside the copy keep those pixels when they
    // are dropped below.
    if (dstLine > 0)
    {
        esp_cache_msync(dst, dstSpan, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    m_Reports = 0;
    xSemaphoreTake(s.done, 0);
    gdma_start(s.rx, gdma_link_get_head_addr(s.rxList));
    gdma_start(s.tx, gdma_link_get_head_addr(s.txList));
    const bool done = xSemaphoreTake(s.done, pdMS_TO_TICKS(100)) == pdTRUE;
    if (!done)
    {
        gdma_reset(s.tx);
        gdma_reset(s.rx);
        if (!m_SaidTimeout)
        {
            m_SaidTimeout = true;
            DEKI_LOG_WARNING("DmaCopy (%s): a copy of %d rows did not finish in 100 ms; it is copied on the CPU",
                             m_Name, static_cast<int>(height));
        }
    }

    // The destination's old lines dropped from the cache, so the CPU reads
    // what the DMA wrote: whole lines, rounded out (written back above).
    if (dstLine > 0)
    {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(dst) & ~(static_cast<uintptr_t>(dstLine) - 1);
        const uintptr_t end =
            (reinterpret_cast<uintptr_t>(dst) + dstSpan + dstLine - 1) & ~(static_cast<uintptr_t>(dstLine) - 1);
        esp_cache_msync(reinterpret_cast<void*>(begin), end - begin, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
    return done ? RectCopyResult::Done : RectCopyResult::Refused;
}

namespace
{
DmaEngine s_RectEngine("rects");
}

bool HasDmaRectCopy()
{
    return true;
}

QuadBlit::RectCopyResult DmaCopyRect(const uint8_t* src, int32_t srcStrideBytes, uint16_t* dst,
                                     int32_t dstStridePixels, int32_t width, int32_t height, bool mayRun)
{
    if (width <= 0)
    {
        return QuadBlit::RectCopyResult::Refused;
    }
    return s_RectEngine.Copy(src, static_cast<size_t>(srcStrideBytes), reinterpret_cast<uint8_t*>(dst),
                             static_cast<size_t>(dstStridePixels) * 2, static_cast<size_t>(width) * 2, height, mayRun);
}

void DmaCopyWait(int32_t rows)
{
    s_RectEngine.Wait(rows);
}

}  // namespace DekiEsp32::Blit

#else

namespace DekiEsp32::Blit
{

struct DmaEngine::State
{
};

QuadBlit::RectCopyResult DmaEngine::Copy(const uint8_t*, size_t, uint8_t*, size_t, size_t, int32_t, bool)
{
    return QuadBlit::RectCopyResult::Refused;
}

void DmaEngine::Wait(int32_t) {}

void DmaEngine::OnReport() {}

bool HasDmaRectCopy()
{
    return false;
}

QuadBlit::RectCopyResult DmaCopyRect(const uint8_t*, int32_t, uint16_t*, int32_t, int32_t, int32_t, bool)
{
    return QuadBlit::RectCopyResult::Refused;
}

void DmaCopyWait(int32_t) {}

}  // namespace DekiEsp32::Blit

#endif
