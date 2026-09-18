#pragma once

#include <deki/providers/ITimeProvider.h>

// Guarded because this header now lives in the package rather than the engine,
// and the package's sources are also compiled for the editor's own DLL on a
// desktop, where ESP-IDF's headers do not exist. It was unguarded while the
// engine owned it, since nothing but a firmware build ever reached it.
#if defined(ESP32)
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace Deki
{


class ESP32TimeProvider : public ITimeProvider
{
public:
#if defined(ESP32)
    uint32_t GetTicksMs() const override
    {
        return (uint32_t)(esp_timer_get_time() / 1000);
    }

    void DelayMs(uint32_t ms) const override
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
#else
    // Declared but inert off-target, so the editor can still name the type
    // while the package's sources compile on a desktop.
    uint32_t GetTicksMs() const override { return 0; }
    void DelayMs(uint32_t) const override {}
#endif
};

}  // namespace Deki
