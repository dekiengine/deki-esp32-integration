#pragma once

#include <cstdint>
#include "SetupComponent.h"
#include "reflection/DekiProperty.h"
#include "ESP32HALModule.h"

/**
 * @brief Component to configure and initialize ESP32 serial command handling
 *
 * Add this component to your boot prefab to enable serial command processing
 * for editor communication (storage mode, status queries, etc.).
 *
 * Registers a per-frame update callback via DekiEngine::RegisterUpdate()
 * to process incoming serial commands.
 *
 * Inherits from SetupComponent to participate in boot sequence.
 */
class DEKI_ESP32_HAL_API ESP32SerialSetup : public SetupComponent
{
public:
    DEKI_COMPONENT(ESP32SerialSetup, SetupComponent, "ESP32 HAL", "c9d4eaf6-5081-4c3d-bf27-a04f9e6d8c51", "DEKI_FEATURE_ESP32_SERIAL_SETUP")

    DEKI_EXPORT
    DEKI_TOOLTIP("Serial baud rate")
    DEKI_RANGE(9600, 921600)
    int32_t baud_rate = 115200;

    void Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "ESP32 Serial Commands"; }
};

// Generated property metadata
#include "generated/ESP32SerialSetup.gen.h"
