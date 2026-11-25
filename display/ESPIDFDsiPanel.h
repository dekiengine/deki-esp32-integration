#pragma once

// deki-display's MIPI-DSI panel on ESP-IDF. Installing this package does not
// bring deki-display: a project without it builds nothing here.
#if __has_include("MipiDsi.h")

#include "MipiDsi.h"  // from deki-display

#include <memory>

namespace DekiEsp32
{

/// A MIPI-DSI panel in video mode on ESP-IDF's esp_lcd DSI driver (the
/// ESP32-P4): commands over the DBI interface, then frames streamed by DMA
/// from frame buffers the driver allocates in PSRAM. Null on a chip without
/// DSI, with an error saying so.
///
/// The DSI PHY needs its supply first: on the ESP32-P4 that is LDO channel 3
/// at 2.5 V, which the board's boot scene turns on (ESP32LdoSetup) before the
/// panel.
std::unique_ptr<DekiDisplay::IDsiPanel> CreateESPIDFDsiPanel(const DekiDisplay::DsiPanelConfig& config);

}  // namespace DekiEsp32

#endif  // __has_include("MipiDsi.h")
