#pragma once

// deki-display's parallel panel bus on ESP-IDF. Installing this package does
// not bring deki-display: a project without it builds nothing here.
#if __has_include("ParallelBus.h")

#include "ParallelBus.h"  // from deki-display

#include <memory>

namespace DekiEsp32
{

/// A parallel (8080) panel bus on ESP-IDF's esp_lcd i80 driver: the LCD_CAM
/// peripheral on an ESP32-S3, I2S on an ESP32. Null on a chip with neither,
/// with an error saying so.
std::unique_ptr<DekiDisplay::IParallelBus> CreateESPIDFParallelBus(const DekiDisplay::ParallelBusConfig& config);

}  // namespace DekiEsp32

#endif  // __has_include("ParallelBus.h")
