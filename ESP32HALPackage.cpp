// Package entry point of the deki-esp32-hal DLL: exports the standard Deki
// plugin interface so the editor can load it and find its ESP32 HAL
// components.

#include "ESP32HALPackage.h"
#include <deki/interop/Plugin.h>
#include "ESP32SerialSetup.h"
#include <deki/reflection/ComponentRegistry.h>
#include <deki/reflection/ComponentFactory.h>

#if defined(ESP32)
#include "ESP32MemoryProvider.h"
#include "ESP32FileSystem.h"
#include "ESP32TimeProvider.h"
#include <deki/providers/Memory.h>
#include <deki/providers/FileSystem.h>
#include <deki/Time.h>
#include "sd/ESPIDFSDCard.h"
#include "DekiSDCard.h"  // from deki-sdcard
#include "i2c/ESPIDFI2C.h"
#include "DekiI2C.h"  // from deki-i2c
#include "gpio/ESPIDFGPIO.h"
#include "DekiGPIO.h"  // from deki-gpio
#include "uart/ESPIDFUART.h"
#include "DekiUART.h"  // from deki-uart
#include "i2s/ESPIDFI2S.h"
#include "DekiI2S.h"  // from deki-i2s
#include "blit/S3PIEBlitKernels.h"
#include "wifi/ESPIDFWiFi.h"
#include "DekiWiFi.h"  // from deki-wifi
#include "ble/ESPIDFBLE.h"
#include "DekiBLE.h"  // from deki-ble
#include "http/ESPIDFHttpClient.h"
#include "DekiHttp.h"  // from deki-http
#include "power/ESPIDFPower.h"
#include <deki/providers/Power.h>
#endif

#if defined(ESP32)
#include <deki/Main.h>
#endif

extern void DekiESP32HALRegisterComponents();
extern int DekiESP32HALGetAutoComponentCount();
extern const Deki::ComponentMeta* DekiESP32HALGetAutoComponentMeta(int index);

namespace DekiEsp32
{

// Registers the ESP32 hardware backends at static-init time.
#if defined(ESP32)

namespace
{
struct ESP32BackendInit
{
    ESP32BackendInit()
    {
        Deki::Memory::SetBackend(new Deki::ESP32MemoryProvider());
        Deki::FileSystem::SetFileSystem(new Deki::ESP32FileSystem());
        Deki::Time::SetTimeProvider(std::make_unique<Deki::ESP32TimeProvider>());
        DekiSdCard::DekiSDCard::SetFactory([]() -> DekiSdCard::IDekiSDCard* { return new ESPIDFSDCard(); });
        DekiI2c::DekiI2C::SetFactory([]() -> DekiI2c::IDekiI2C* { return new ESPIDFI2C(); });
        DekiUart::DekiUART::SetFactory([]() -> DekiUart::IDekiUART* { return new ESPIDFUART(); });
        DekiI2s::DekiI2S::SetFactory([]() -> DekiI2s::IDekiI2S* { return new ESPIDFI2S(); });

        // Pins: one for the whole chip, like WiFi below.
        static ESPIDFGPIO s_Gpio;
        s_Gpio.Initialize();
        DekiGpio::DekiGPIO::SetCurrent(&s_Gpio);

        // WiFi: one active driver. Like the rest of this block, it is never
        // freed.
        static ESPIDFWiFi s_WiFi;
        s_WiFi.Initialize();
        DekiWifi::DekiWiFi::SetCurrent(&s_WiFi);

        // BLE: one active driver, on NimBLE. Never freed, like WiFi.
        static ESPIDFBLE s_BLE;
        s_BLE.Initialize();
        DekiBle::DekiBLE::SetCurrent(&s_BLE);

        // HTTP: the ESP-IDF client behind deki-http's facade. Consumers
        // (location and weather providers) reach it through DekiHttp::Get /
        // PostJson, never through the concrete type.
        static ESPIDFHttpClient s_Http;
        DekiHttp::SetCurrent(&s_Http);

        // Power: the light-sleep driver. The app sets idle timeout and wake
        // GPIO at run time through Deki::Power::GetCurrent()->Set*.
        static ESPIDFPower s_Power;
        s_Power.Initialize();
        Deki::Power::SetCurrent(&s_Power);

#if defined(CONFIG_IDF_TARGET_ESP32S3)
        // S3 PIE SIMD blit kernels. Only verified kernels are registered;
        // QuadBlit runs its scalar inner loop for the other ops. See
        // blit/S3PIEBlitKernels.cpp.
        QuadBlit::RegisterKernel(QuadBlit::KernelOp::RGB565CopyRow, &DekiEsp32::Blit::S3PIERGB565CopyRow);
#endif
    }
};
static ESP32BackendInit s_Esp32Init;
}  // namespace

// The exports below are C symbols at global scope; the package's own
// registration helpers and statics live in its namespace.
using namespace DekiEsp32;

extern "C" void app_main(void)
{
    Deki::Main();
}

#endif  // ESP32
}  // namespace DekiEsp32

#ifdef DEKI_EDITOR

// Set once registered, so a second call does not register twice.
static bool s_ESP32HALRegistered = false;

extern "C"
{
    /// Registers the package's components once and returns how many there are.
    DEKI_ESP32_HAL_API int DekiESP32HALEnsureRegistered(void)
    {
        if (s_ESP32HALRegistered)
        {
            return ::DekiESP32HALGetAutoComponentCount();
        }
        s_ESP32HALRegistered = true;

        // Generated: registers every ESP32 HAL component with ComponentRegistry and ComponentFactory.
        ::DekiESP32HALRegisterComponents();

        return ::DekiESP32HALGetAutoComponentCount();
    }

    // =============================================================================
    // Plugin metadata, for loading as a DLL
    // =============================================================================

    DEKI_PLUGIN_API const char* DekiPluginGetName(void)
    {
        return "Deki ESP32 HAL Package";
    }

    DEKI_PLUGIN_API const char* DekiPluginGetVersion(void)
    {
#ifdef DEKI_PACKAGE_VERSION
        return DEKI_PACKAGE_VERSION;
#else
        return "0.0.0-dev";
#endif
    }

    DEKI_PLUGIN_API int DekiPluginInit(void)
    {
        return 0;
    }

    DEKI_PLUGIN_API void DekiPluginShutdown(void)
    {
        s_ESP32HALRegistered = false;
    }

    DEKI_PLUGIN_API int DekiPluginGetComponentCount(void)
    {
        return ::DekiESP32HALGetAutoComponentCount();
    }

    DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPluginGetComponentMeta(int index)
    {
        return ::DekiESP32HALGetAutoComponentMeta(index);
    }

    DEKI_PLUGIN_API void DekiPluginRegisterComponents(void)
    {
        DekiESP32HALEnsureRegistered();
    }

    // =============================================================================
    // Package-specific API, named so linked DLLs do not clash
    // =============================================================================

    DEKI_ESP32_HAL_API const char* DekiESP32HALGetName(void)
    {
        return "ESP32 HAL";
    }

}  // extern "C"

#else  // !DEKI_EDITOR

// Runtime builds register components through static initializers or explicit
// calls from the application.

#endif  // DEKI_EDITOR
