/**
 * @file ESP32HALModule.cpp
 * @brief Module entry point for deki-esp32-hal DLL
 *
 * This file exports the standard Deki plugin interface so the editor
 * can load deki-esp32-hal.dll and discover available ESP32 HAL components.
 */

#include "ESP32HALModule.h"
#include "interop/DekiPlugin.h"
#include "modules/DekiModuleFeatureMeta.h"
#include "ESP32SerialSetup.h"
#include "reflection/ComponentRegistry.h"
#include "reflection/ComponentFactory.h"

// Direct backend registration for ESP32 hardware
#if defined(ESP32)
#include "platforms/esp32/ESP32MemoryProvider.h"
#include "platforms/esp32/ESP32FileSystem.h"
#include "platforms/esp32/ESP32TimeProvider.h"
#include "providers/DekiMemoryProvider.h"
#include "providers/DekiFileSystemProvider.h"
#include "DekiTime.h"
#include "sd/ESPIDFSDCard.h"
#include "providers/DekiSDCardProvider.h"

namespace
{
struct ESP32BackendInit {
    ESP32BackendInit() {
        DekiMemoryProvider::SetBackend(new ESP32MemoryProvider());
        DekiFileSystemProvider::SetFileSystem(new ESP32FileSystem());
        DekiTime::SetTimeProvider(std::make_unique<ESP32TimeProvider>());
        DekiSDCardProvider::SetFactory([]() -> IDekiSDCard* { return new ESPIDFSDCard(); });
    }
};
static ESP32BackendInit s_esp32_init;
}

#include "DekiMain.h"
extern "C" void app_main(void) { DekiMain(); }

#endif // ESP32

#ifdef DEKI_EDITOR

// Auto-generated registration helpers
extern void DekiESP32HAL_RegisterComponents();
extern int DekiESP32HAL_GetAutoComponentCount();
extern const DekiComponentMeta* DekiESP32HAL_GetAutoComponentMeta(int index);

// Track if already registered to avoid duplicates
static bool s_ESP32HALRegistered = false;

extern "C" {

/**
 * @brief Ensure deki-esp32-hal module is loaded and components are registered
 */
DEKI_ESP32_HAL_API int DekiESP32HAL_EnsureRegistered(void)
{
    if (s_ESP32HALRegistered)
        return DekiESP32HAL_GetAutoComponentCount();
    s_ESP32HALRegistered = true;

    // Auto-generated: registers all ESP32 HAL components with ComponentRegistry + ComponentFactory
    DekiESP32HAL_RegisterComponents();

    return DekiESP32HAL_GetAutoComponentCount();
}

// =============================================================================
// Plugin metadata (for dynamic loading compatibility)
// =============================================================================

DEKI_PLUGIN_API const char* DekiPlugin_GetName(void)
{
    return "Deki ESP32 HAL Module";
}

DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void)
{
#ifdef DEKI_MODULE_VERSION
    return DEKI_MODULE_VERSION;
#else
    return "0.0.0-dev";
#endif
}

DEKI_PLUGIN_API const char* DekiPlugin_GetReflectionJson(void)
{
    return "{}";
}

DEKI_PLUGIN_API int DekiPlugin_Init(void)
{
    return 0;
}

DEKI_PLUGIN_API void DekiPlugin_Shutdown(void)
{
    s_ESP32HALRegistered = false;
}

DEKI_PLUGIN_API int DekiPlugin_GetComponentCount(void)
{
    return DekiESP32HAL_GetAutoComponentCount();
}

DEKI_PLUGIN_API const DekiComponentMeta* DekiPlugin_GetComponentMeta(int index)
{
    return DekiESP32HAL_GetAutoComponentMeta(index);
}

DEKI_PLUGIN_API void DekiPlugin_RegisterComponents(void)
{
    DekiESP32HAL_EnsureRegistered();
}

// =============================================================================
// Module Feature API
// =============================================================================

static const DekiModuleFeatureInfo s_Features[] = {
    {"serial", "Serial Commands", "Editor serial communication", false},
};

DEKI_PLUGIN_API int DekiPlugin_GetFeatureCount(void)
{
    return sizeof(s_Features) / sizeof(s_Features[0]);
}

DEKI_PLUGIN_API const DekiModuleFeatureInfo* DekiPlugin_GetFeature(int index)
{
    if (index < 0 || index >= DekiPlugin_GetFeatureCount())
        return nullptr;
    return &s_Features[index];
}

// =============================================================================
// Module-specific feature API (for linked DLL access without name conflicts)
// =============================================================================

DEKI_ESP32_HAL_API const char* DekiESP32HAL_GetName(void)
{
    return "ESP32 HAL";
}

DEKI_ESP32_HAL_API int DekiESP32HAL_GetFeatureCount(void)
{
    return DekiPlugin_GetFeatureCount();
}

DEKI_ESP32_HAL_API const DekiModuleFeatureInfo* DekiESP32HAL_GetFeature(int index)
{
    return DekiPlugin_GetFeature(index);
}

} // extern "C"

#else // !DEKI_EDITOR - Runtime registration

// For runtime builds, component registration happens via static initializers
// or explicit calls from the application

#endif // DEKI_EDITOR
