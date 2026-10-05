#pragma once

#include <cstdint>
#include <deki/SetupComponent.h>
#include <deki/reflection/Property.h>
#include "ESP32HALPackage.h"

namespace DekiEsp32
{

/// Starts ESP32 serial command handling, so the editor can talk to the
/// device (storage mode, status queries, etc.). Add it to the boot scene.
///
/// Registers a per-frame update through Deki::Engine::RegisterUpdate() that
/// handles incoming serial commands.
DEKI_CATEGORY("ESP32 HAL")
DEKI_DESCRIPTION("Answers the editor's commands over the ESP32's serial port.")
class DEKI_ESP32_HAL_API ESP32SerialSetup : public Deki::SetupComponent
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("Serial baud rate")
    DEKI_RANGE(9600, 921600)
    int32_t baudRate = 115200;

    void Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "ESP32 Serial Commands"; }
};

}  // namespace DekiEsp32
