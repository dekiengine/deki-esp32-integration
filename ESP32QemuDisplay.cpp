#include "ESP32QemuDisplay.h"

#if defined(ESP32) && !defined(DEKI_EDITOR) && defined(DEKI_FEATURE_ESP32_QEMU_DISPLAY)
#include <deki/LogSystem.h>
#include <soc/syscon_reg.h>
#endif

namespace DekiEsp32
{

#if defined(ESP32) && !defined(DEKI_EDITOR) && defined(DEKI_FEATURE_ESP32_QEMU_DISPLAY)

namespace
{
// The panel's registers, as Espressif's QEMU maps them.
struct QemuPanelRegisters
{
    uint32_t version;
    uint32_t size;          // width << 16 | height
    uint32_t updateFrom;    // x << 16 | y
    uint32_t updateTo;      // x << 16 | y, exclusive
    const void* content;    // the pixels of that rectangle, packed
    uint32_t updateStatus;  // bit 0: set to start, QEMU clears it when done
    uint32_t bpp;           // 16 or 32
};
volatile QemuPanelRegisters* const kPanel = reinterpret_cast<volatile QemuPanelRegisters*>(0x21000000);

// "QEMU" as a 32-bit value, in the word just before SYSCON's date register.
constexpr uint32_t kQemuMark = 0x51454d55;
}  // namespace

bool ESP32QemuDisplay::RunningInQemu()
{
    return REG_READ(SYSCON_DATE_REG - 4) == kQemuMark;
}

bool ESP32QemuDisplay::Initialize(int32_t width, int32_t height)
{
    if (!RunningInQemu())
    {
        DEKI_LOG_ERROR("ESP32QemuDisplay: this board is not running under QEMU; the QEMU panel exists only there");
        return false;
    }
    if (m_Format != Deki::ColorFormat::RGB565 && m_Format != Deki::ColorFormat::ARGB8888)
    {
        DEKI_LOG_ERROR("ESP32QemuDisplay: the QEMU panel shows RGB565 or ARGB8888");
        return false;
    }
    if (width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF)
    {
        DEKI_LOG_ERROR("ESP32QemuDisplay: %dx%d is not a panel size", (int)width, (int)height);
        return false;
    }
    kPanel->size = (static_cast<uint32_t>(width) << 16) | static_cast<uint32_t>(height);
    kPanel->bpp = m_Format == Deki::ColorFormat::RGB565 ? 16u : 32u;
    m_Width = width;
    m_Height = height;
    m_Initialized = true;
    DEKI_LOG_INFO("ESP32QemuDisplay: %dx%d panel, %s", (int)width, (int)height,
                  m_Format == Deki::ColorFormat::RGB565 ? "RGB565" : "ARGB8888");
    return true;
}

void ESP32QemuDisplay::Shutdown()
{
    m_Initialized = false;
}

void ESP32QemuDisplay::Present(const uint8_t* framebuffer, int width, int height, Deki::ColorFormat format)
{
    if (!m_Initialized || !framebuffer || width != m_Width || height != m_Height || format != m_Format)
        return;
    kPanel->updateFrom = 0;
    kPanel->updateTo = (static_cast<uint32_t>(width) << 16) | static_cast<uint32_t>(height);
    kPanel->content = framebuffer;
    kPanel->updateStatus = 1;
    // QEMU copies the pixels and clears the flag before this returns; waiting
    // for it means the engine can draw the next frame into the same buffer.
    while (kPanel->updateStatus & 1u)
    {
    }
}

void ESP32QemuDisplay::GetDisplaySize(int32_t* width, int32_t* height) const
{
    if (width) *width = m_Width;
    if (height) *height = m_Height;
}

#else

bool ESP32QemuDisplay::RunningInQemu() { return false; }
bool ESP32QemuDisplay::Initialize(int32_t, int32_t) { return false; }
void ESP32QemuDisplay::Shutdown() {}
void ESP32QemuDisplay::Present(const uint8_t*, int, int, Deki::ColorFormat) {}
void ESP32QemuDisplay::GetDisplaySize(int32_t* width, int32_t* height) const
{
    if (width) *width = 0;
    if (height) *height = 0;
}

#endif

}  // namespace DekiEsp32
