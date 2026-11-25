#pragma once

#include <cstddef>
#include <cstdint>

#include "deki-rendering/QuadBlit.h"

// Large copies by the ESP32-P4's AXI DMA: QuadBlit's rectangle copy
// (QuadBlit::RegisterRectCopy), and the DSI panel's strips into its frame
// buffers. Only the P4 has them.

namespace DekiEsp32::Blit
{

/// One AXI DMA channel pair doing one copy at a time: `height` rows of
/// `rowBytes`, from `src` (rows `srcStride` bytes apart) to `dst` (rows
/// `dstStride` bytes apart), one transfer with a descriptor per row on each
/// side. Its channels start on first use.
///
/// With `mayRun`, and the destination's rows on whole cache lines with no gap
/// between them, it starts the copy and returns Running: the destination's
/// lines are dropped from the cache first (every byte of them is
/// overwritten), and Wait waits for rows as they arrive, top to bottom.
/// Otherwise it waits for the copy and returns Done, the cache written back
/// over the destination before and dropped after. Refused when the DMA cannot
/// take it (a row longer than one descriptor carries, memory it cannot reach,
/// no channel): nothing was copied.
class DmaEngine
{
public:
    explicit DmaEngine(const char* name) : m_Name(name) {}

    QuadBlit::RectCopyResult Copy(const uint8_t* src, size_t srcStride, uint8_t* dst, size_t dstStride,
                                  size_t rowBytes, int32_t height, bool mayRun);

    /// Waits until `rows` rows of the copy running are there (all of them
    /// for INT32_MAX). A copy that stalls for 100 ms is stopped and its
    /// remaining rows copied on the CPU, said once in the log.
    void Wait(int32_t rows);

    /// A copy is running.
    bool Running() const { return m_Running; }

    // The interrupt's side, public for its callback.
    void OnReport();

private:
    bool EnsureChannels();
    bool EnsureLists(int32_t rows);
    int32_t RowsDone() const;

    const char* m_Name;
    struct State;
    State* m_State = nullptr;
    bool m_Failed = false;
    bool m_SaidTimeout = false;
    bool m_Running = false;
    volatile int32_t m_Reports = 0;  // EOFs since the copy started, counted by the interrupt
    bool m_Whole = false;            // the copy running is one block: it reports once, at its end
};

/// QuadBlit's rectangle copy (RegisterRectCopy) on its own engine.
QuadBlit::RectCopyResult DmaCopyRect(const uint8_t* src, int32_t srcStrideBytes, uint16_t* dst,
                                     int32_t dstStridePixels, int32_t width, int32_t height, bool mayRun);
/// RegisterRectCopy's wait, for that engine.
void DmaCopyWait(int32_t rows);

/// Copies smaller than this stay on the CPU, which starts at once.
constexpr int32_t kDmaCopyMinPixels = 65536;

/// True when this build has them (the ESP32-P4).
bool HasDmaRectCopy();

}  // namespace DekiEsp32::Blit
