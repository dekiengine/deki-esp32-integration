#pragma once

#include <cstdint>

#include "deki-rendering/QuadBlit.h"

// Large blits on both cores of the ESP32-P4, for QuadBlit::RegisterParallelRows:
// the lower half of a blit's rows on the core the game does not run on.
// Only the P4 registers it: on the S3 that core sends frames to SPI panels
// (the present worker).

namespace DekiEsp32::Blit
{

/// Runs job(context, splitY, endY) on the other core while running
/// job(context, startY, splitY) here; returns when both are done. Its worker
/// task starts on first use. False, having run nothing, when called from the
/// worker's own core or when the worker could not start.
bool DualCoreRows(QuadBlit::RowsJob job, void* context, int32_t startY, int32_t splitY, int32_t endY);

/// Blits smaller than this stay on one core: waking the other costs more
/// than it saves.
constexpr int32_t kDualCoreMinPixels = 2048;

/// True when this build has it: a chip with two cores (the ESP32-P4).
bool HasDualCoreRows();

}  // namespace DekiEsp32::Blit
