#pragma once

// deki-display's present worker on ESP-IDF. Installing this package does not
// bring deki-display: a project without it (a board on LovyanGFX) builds
// nothing here.
#if __has_include("PanelDisplay.h")

#include "PanelDisplay.h"  // from deki-display

namespace DekiEsp32
{

/// deki-display's present worker on FreeRTOS: a task pinned to the core the
/// engine is not on, so a frame is sent while the next is drawn. On a
/// single-core chip it starts nothing, and frames are sent as they are drawn.
const DekiDisplay::PanelDisplay::WorkerBackend& ESPIDFPresentWorker();

}  // namespace DekiEsp32

#endif  // __has_include("PanelDisplay.h")
