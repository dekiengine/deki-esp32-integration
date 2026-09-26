#include "ESP32QemuDisplaySetup.h"

#include <deki/LogSystem.h>

#if defined(ESP32) && !defined(DEKI_EDITOR) && defined(DEKI_FEATURE_ESP32_QEMU_DISPLAY)
#include <deki/Engine.h>

#include <memory>

#include "ESP32QemuDisplay.h"
#endif

namespace DekiEsp32
{

#if defined(ESP32) && !defined(DEKI_EDITOR) && defined(DEKI_FEATURE_ESP32_QEMU_DISPLAY)

// The engine holds a pointer; the package owns the display for the program's
// lifetime.
static std::unique_ptr<ESP32QemuDisplay> s_QemuDisplay;

void ESP32QemuDisplaySetup::Setup(SetupCallback onComplete)
{
    s_QemuDisplay = std::make_unique<ESP32QemuDisplay>();
    s_QemuDisplay->SetColorFormat(format == QemuPanelFormat::ARGB8888 ? Deki::ColorFormat::ARGB8888
                                                                       : Deki::ColorFormat::RGB565);
    if (!s_QemuDisplay->Initialize(width, height))
    {
        s_QemuDisplay.reset();
        onComplete(false);
        return;
    }
    Deki::Engine::GetInstance().SetDisplay(s_QemuDisplay.get(), "QEMU");
    onComplete(true);
}

#else

void ESP32QemuDisplaySetup::Setup(SetupCallback onComplete)
{
    // The editor draws the Play view itself; there is no QEMU here.
    onComplete(true);
}

#endif

}  // namespace DekiEsp32
