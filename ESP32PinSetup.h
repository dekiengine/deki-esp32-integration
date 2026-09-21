#pragma once

#include <cstdint>
#include <deki/SetupComponent.h>
#include <deki/reflection/Property.h>
#include "ESP32HALPackage.h"

namespace DekiEsp32
{

/**
 * @brief Drives one GPIO to a fixed level during boot
 *
 * Boards gate things behind a pin: a power rail that feeds the display and
 * the touch controller, the chip select of a second device sharing the
 * display's SPI bus, an amplifier's enable. Such a pin has to be at its level
 * before the setup step that needs it, so this is a setup step of its own:
 * list it in PlatformSetupComponent's setupComponents ahead of that step.
 *
 * One pin per component; a board that needs three adds three.
 */
DEKI_CATEGORY("ESP32 HAL")
DEKI_DESCRIPTION("Drives a GPIO high or low at boot: a power enable, or the chip select of an unused device on a shared bus.")
class DEKI_ESP32_HAL_API ESP32PinSetup : public Deki::SetupComponent
{
public:

    DEKI_EXPORT
    DEKI_TOOLTIP("GPIO number (-1 = do nothing)")
    DEKI_RANGE(-1, 48)
    int32_t pin = -1;

    DEKI_EXPORT
    DEKI_TOOLTIP("Drive the pin high (off = low)")
    bool high = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Milliseconds to wait afterwards, for a power rail to come up before the next setup step")
    DEKI_RANGE(0, 5000)
    int32_t settleMs = 0;

    void Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "ESP32 Pin"; }
};

// Generated property metadata

}  // namespace DekiEsp32
