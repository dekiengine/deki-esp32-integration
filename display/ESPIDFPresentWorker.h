#pragma once

#include "PanelDisplay.h"  // from deki-display

namespace DekiEsp32
{

/// deki-display's present worker on FreeRTOS: a task pinned to the core the
/// engine is not on, so a frame is sent while the next is drawn. On a
/// single-core chip it starts nothing, and frames are sent as they are drawn.
const DekiDisplay::PanelDisplay::WorkerBackend& ESPIDFPresentWorker();

}  // namespace DekiEsp32
