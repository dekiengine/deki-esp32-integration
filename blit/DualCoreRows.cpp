// Large blits on both cores of the ESP32-P4. See DualCoreRows.h.

#include "DualCoreRows.h"

#if defined(ESP32)
#include <sdkconfig.h>  // CONFIG_IDF_TARGET_*, CONFIG_FREERTOS_UNICORE
#endif

#if defined(ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4) && !defined(CONFIG_FREERTOS_UNICORE)

#include <deki/LogSystem.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace DekiEsp32::Blit
{

namespace
{

// The lower half of the blit being drawn, set before the worker is woken and
// read only by it until it wakes the caller back: the notifications order
// the two cores' memory.
struct Half
{
    QuadBlit::RowsJob job = nullptr;
    void* context = nullptr;
    int32_t startY = 0;
    int32_t endY = 0;
    TaskHandle_t caller = nullptr;
};
Half s_Half;
TaskHandle_t s_Worker = nullptr;
BaseType_t s_WorkerCore = -1;
bool s_Failed = false;

void Worker(void*)
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_Half.job(s_Half.context, s_Half.startY, s_Half.endY);
        xTaskNotifyGive(s_Half.caller);
    }
}

bool EnsureWorker()
{
    if (s_Worker)
    {
        return true;
    }
    if (s_Failed)
    {
        return false;
    }
    // The core the game is not drawing on, above everything but the
    // system's own tasks there, so the half starts at once.
    s_WorkerCore = xPortGetCoreID() == 0 ? 1 : 0;
    if (xTaskCreatePinnedToCore(&Worker, "deki_rows", 8192, nullptr, configMAX_PRIORITIES - 3, &s_Worker,
                                s_WorkerCore) != pdPASS)
    {
        s_Failed = true;
        s_Worker = nullptr;
        DEKI_LOG_WARNING("DualCoreRows: no worker on core %d; blits stay on one core", static_cast<int>(s_WorkerCore));
        return false;
    }
    DEKI_LOG_INFO("DualCoreRows: large blits draw their lower rows on core %d", static_cast<int>(s_WorkerCore));
    return true;
}

}  // namespace

bool HasDualCoreRows()
{
    return true;
}

bool DualCoreRows(QuadBlit::RowsJob job, void* context, int32_t startY, int32_t splitY, int32_t endY)
{
    if (!EnsureWorker() || xPortGetCoreID() == s_WorkerCore)
    {
        return false;
    }
    s_Half.job = job;
    s_Half.context = context;
    s_Half.startY = splitY;
    s_Half.endY = endY;
    s_Half.caller = xTaskGetCurrentTaskHandle();
    xTaskNotifyGive(s_Worker);
    job(context, startY, splitY);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return true;
}

}  // namespace DekiEsp32::Blit

#else

namespace DekiEsp32::Blit
{

bool HasDualCoreRows()
{
    return false;
}

bool DualCoreRows(QuadBlit::RowsJob, void*, int32_t, int32_t, int32_t)
{
    return false;
}

}  // namespace DekiEsp32::Blit

#endif
