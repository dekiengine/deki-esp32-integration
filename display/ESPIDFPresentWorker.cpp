#include "ESPIDFPresentWorker.h"

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/soc_caps.h>
#endif

namespace DekiEsp32
{

namespace
{

#if defined(ESP32)
struct Start
{
    void (*run)(void*);
    void* arg;
};

void TaskMain(void* param)
{
    const Start start = *static_cast<Start*>(param);
    delete static_cast<Start*>(param);
    start.run(start.arg);
    // A FreeRTOS task must not return.
    vTaskDelete(nullptr);
}
#endif

bool StartWorker(void (*run)(void*), void* arg)
{
#if defined(ESP32) && !defined(CONFIG_FREERTOS_UNICORE) && (SOC_CPU_CORES_NUM > 1)
    // The other core from the one starting it, which runs the engine.
    const BaseType_t core = xPortGetCoreID() == 0 ? 1 : 0;
    auto* start = new Start{ run, arg };
    if (xTaskCreatePinnedToCore(&TaskMain, "deki_present", 4096, start, 5, nullptr, core) != pdPASS)
    {
        delete start;
        return false;
    }
    return true;
#else
    (void)run;
    (void)arg;
    return false;
#endif
}

void YieldWorker()
{
#if defined(ESP32)
    vTaskDelay(1);
#endif
}

const DekiDisplay::PanelDisplay::WorkerBackend kWorker{ &StartWorker, &YieldWorker };

}  // namespace

const DekiDisplay::PanelDisplay::WorkerBackend& ESPIDFPresentWorker()
{
    return kWorker;
}

}  // namespace DekiEsp32
