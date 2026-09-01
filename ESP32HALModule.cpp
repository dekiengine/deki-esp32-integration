/**
 * @file ESP32HALPackage.cpp
 * @brief Package entry point for deki-esp32-hal DLL
 *
 * This file exports the standard Deki plugin interface so the editor
 * can load deki-esp32-hal.dll and discover available ESP32 HAL components.
 */

#include "ESP32HALPackage.h"
#include "interop/DekiPlugin.h"
#include "ESP32SerialSetup.h"
#include "reflection/ComponentRegistry.h"
#include "reflection/ComponentFactory.h"

// Direct backend registration for ESP32 hardware
#if defined(ESP32)
#include "platforms/esp32/ESP32MemoryProvider.h"
#include "platforms/esp32/ESP32FileSystem.h"
#include "platforms/esp32/ESP32TimeProvider.h"
#include "providers/DekiMemory.h"
#include "providers/DekiFileSystem.h"
#include "DekiTime.h"
#include "sd/ESPIDFSDCard.h"
#include "DekiSDCard.h"  // from deki-sdcard
#include "i2c/ESPIDFI2C.h"
#include "DekiI2C.h"  // from deki-i2c
#include "uart/ESPIDFUART.h"
#include "DekiUART.h"  // from deki-uart
#include "i2s/ESPIDFI2S.h"
#include "DekiI2S.h"  // from deki-i2s
#include "blit/S3PIEBlitKernels.h"
#include "wifi/ESPIDFWiFi.h"
#include "DekiWiFi.h"           // from deki-wifi
#include "ble/ESPIDFBLE.h"
#include "DekiBLE.h"            // from deki-ble
#include "http/ESPIDFHttpClient.h"
#include "DekiHttp.h"           // from deki-http
#include "power/ESPIDFPower.h"
#include "providers/DekiPower.h"

namespace
{
struct ESP32BackendInit {
    ESP32BackendInit() {
        DekiMemory::SetBackend(new ESP32MemoryProvider());
        DekiFileSystem::SetFileSystem(new ESP32FileSystem());
        DekiTime::SetTimeProvider(std::make_unique<ESP32TimeProvider>());
        DekiSDCard::SetFactory([]() -> IDekiSDCard* { return new ESPIDFSDCard(); });
        DekiI2C::SetFactory([]() -> IDekiI2C* { return new ESPIDFI2C(); });
        DekiUART::SetFactory([]() -> IDekiUART* { return new ESPIDFUART(); });
        DekiI2S::SetFactory([]() -> IDekiI2S* { return new ESPIDFI2S(); });

        // WiFi: single-active. The driver instance is intentionally leaked at
        // process exit, matching the rest of this init block.
        static ESPIDFWiFi s_WiFi;
        s_WiFi.Initialize();
        DekiWiFi::SetCurrent(&s_WiFi);

        // BLE: single-active, NimBLE-backed. Same leak rationale as WiFi.
        static ESPIDFBLE s_BLE;
        s_BLE.Initialize();
        DekiBLE::SetCurrent(&s_BLE);

        // HTTP: register ESP-IDF backed client with the abstract facade from
        // deki-http. Consumers (location/weather providers) reach the active
        // client through DekiHttp::Get / PostJson, never via the concrete type.
        static ESPIDFHttpClient s_Http;
        DekiHttp::SetCurrent(&s_Http);

        // Power: light-sleep driver. Idle timeout / wake GPIO are configured
        // by the app at runtime via DekiPower::GetCurrent()->Set*.
        static ESPIDFPower s_Power;
        s_Power.Initialize();
        DekiPower::SetCurrent(&s_Power);

#if defined(CONFIG_IDF_TARGET_ESP32S3)
        // S3 PIE SIMD blit kernels. Only kernels with verified implementations
        // are registered; the dispatcher in QuadBlit runs its scalar inner
        // loop for unregistered ops. See blit/S3PIEBlitKernels.cpp.
        QuadBlit::RegisterKernel(QuadBlit::KernelOp::RGB565_Copy_Row,
                                 &DekiESP32::Blit::S3PIE_RGB565_Copy_Row);
#endif
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
 * @brief Ensure deki-esp32-hal package is loaded and components are registered
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
    return "Deki ESP32 HAL Package";
}

DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void)
{
#ifdef DEKI_PACKAGE_VERSION
    return DEKI_PACKAGE_VERSION;
#else
    return "0.0.0-dev";
#endif
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
// Package-specific feature API (for linked DLL access without name conflicts)
// =============================================================================

DEKI_ESP32_HAL_API const char* DekiESP32HAL_GetName(void)
{
    return "ESP32 HAL";
}

} // extern "C"

#else // !DEKI_EDITOR - Runtime registration

// For runtime builds, component registration happens via static initializers
// or explicit calls from the application

#endif // DEKI_EDITOR
