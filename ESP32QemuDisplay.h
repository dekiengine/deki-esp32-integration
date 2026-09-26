#pragma once

#include <cstdint>
#include <deki/Engine.h>  // Deki::ColorFormat (IDisplay.h only declares it)
#include <deki/providers/IDisplay.h>

namespace DekiEsp32
{

/**
 * @brief The virtual RGB panel of Espressif's QEMU.
 *
 * Espressif's QEMU gives its ESP32 and ESP32-S3 machines a screen that no
 * real chip has: a window QEMU draws, fed through a few memory-mapped
 * registers (a size, an update rectangle, a pointer to the pixels and a start
 * flag). This display drives it, so a firmware build runs its renderer under
 * QEMU and a frame can be captured with QEMU's `screendump`.
 *
 * The register layout follows Espressif's esp_lcd_qemu_rgb component
 * (Apache-2.0); the driver here is Deki's own and needs no component.
 *
 * RGB565 and ARGB8888 are the panel's two formats. The whole frame is pushed
 * on every Present; QEMU copies it before the call returns.
 */
class ESP32QemuDisplay : public Deki::IDisplay
{
public:
    /// True when the firmware is running under Espressif's QEMU, which marks
    /// a register real chips leave unset.
    static bool RunningInQemu();

    /// The panel's format: RGB565 or ARGB8888. Set before Initialize.
    void SetColorFormat(Deki::ColorFormat format) { m_Format = format; }

    bool Initialize(int32_t width, int32_t height) override;
    void Shutdown() override;
    void Present(const uint8_t* framebuffer, int width, int height, Deki::ColorFormat format) override;
    void GetDisplaySize(int32_t* width, int32_t* height) const override;
    Deki::ColorFormat GetColorFormat() const override { return m_Format; }
    bool IsInitialized() const override { return m_Initialized; }
    void RequestFullRefresh() override {}
    bool ProcessEvents() override { return true; }

    // No UI overlays: nothing on this panel composites a second layer.
    void* CreateUIOverlay(int32_t, int32_t) override { return nullptr; }
    bool UpdateUIOverlay(void*, int32_t, int32_t, int32_t, int32_t, const uint32_t*) override { return false; }
    bool UpdateUIOverlayRGB565A8(void*, int32_t, int32_t, int32_t, int32_t, const uint8_t*) override { return false; }
    void DestroyUIOverlay(void*) override {}
    void SetActiveUIOverlay(void*) override {}
    void ClearActiveUIOverlay() override {}

private:
    Deki::ColorFormat m_Format = Deki::ColorFormat::RGB565;
    int32_t m_Width = 0;
    int32_t m_Height = 0;
    bool m_Initialized = false;
};

}  // namespace DekiEsp32
