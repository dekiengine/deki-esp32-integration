#pragma once

#include <cstdint>
#include <deki/SetupComponent.h>
#include <deki/reflection/Property.h>
#include "ESP32HALPackage.h"

namespace DekiEsp32
{

/// The QEMU panel's pixel formats.
enum class QemuPanelFormat : uint8_t
{
    RGB565 = 0,
    ARGB8888 = 1
};

/**
 * @brief Brings up the screen Espressif's QEMU gives an ESP32 machine.
 *
 * For a platform that runs under QEMU rather than on a board: its boot scene
 * takes this in place of a real display's setup, and the game renders into
 * QEMU's window (or a `screendump` of it). On a real chip there is no such
 * panel and the setup fails, saying so.
 */
DEKI_CATEGORY("ESP32 HAL")
DEKI_DESCRIPTION("Opens the screen of Espressif's QEMU, for running a firmware build without a board.")
class DEKI_ESP32_HAL_API ESP32QemuDisplaySetup : public Deki::SetupComponent
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The screen's width in pixels.")
    DEKI_RANGE(1, 4096)
    int32_t width = 320;

    DEKI_EXPORT
    DEKI_TOOLTIP("The screen's height in pixels.")
    DEKI_RANGE(1, 4096)
    int32_t height = 240;

    DEKI_EXPORT
    DEKI_TOOLTIP("The screen's pixel format. The game renders in it.")
    QemuPanelFormat format = QemuPanelFormat::RGB565;

    void Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "QEMU Display"; }
};

// Generated property metadata

}  // namespace DekiEsp32
