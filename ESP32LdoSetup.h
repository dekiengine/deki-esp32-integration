#pragma once

#include <cstdint>
#include <deki/SetupComponent.h>
#include <deki/reflection/Property.h>
#include "ESP32HALPackage.h"

namespace DekiEsp32
{

/// Turns on one of the chip's own voltage regulators (LDO channels) at a set
/// voltage, and keeps it on for the program's life.
///
/// The ESP32-P4 powers parts of a board from its LDOs: its MIPI-DSI PHY needs
/// channel 3 at 2.5 V before a DSI panel starts, and boards feed other rails
/// from channel 4. A boot scene lists one of these per rail, before what the
/// rail powers. Channels 1 and 2 feed the flash and the PSRAM; ESP-IDF sets
/// those itself.
///
/// On a chip without such regulators it fails, saying so; in the editor it
/// does nothing.
DEKI_CATEGORY("ESP32 HAL")
DEKI_DESCRIPTION("Turns on an LDO regulator of the chip (ESP32-P4) at a set voltage, for a rail of the board.")
class DEKI_ESP32_HAL_API ESP32LdoSetup : public Deki::SetupComponent
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The LDO channel, as the datasheet numbers it (3 for LDO_VO3)")
    DEKI_RANGE(1, 4)
    int32_t channel = 3;

    DEKI_EXPORT
    DEKI_TOOLTIP("Its voltage, in millivolts")
    DEKI_RANGE(500, 3300)
    int32_t millivolts = 2500;

    void Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "LDO"; }
};

}  // namespace DekiEsp32
