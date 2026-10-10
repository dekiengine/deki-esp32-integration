#include "ESP32ScreenCapture.h"

#if defined(ESP32)

#include <deki/Engine.h>
#include <deki/Time.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "esp_rom_crc.h"
#include "sdkconfig.h"

// The console the editor talks to. ESP-IDF reads the USB Serial/JTAG console
// only through its driver, which the firmware does not install (the console
// would then go through it too), so that one is read from its FIFO here; a
// UART console is read through stdin, which works without a driver.
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) || defined(CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG)
#include "hal/usb_serial_jtag_ll.h"
#define DEKI_CAPTURE_USB_SERIAL_JTAG 1
#endif

namespace DekiEsp32
{

namespace
{

constexpr char kRequest[] = "DEKI:SCREENSHOT";
constexpr size_t kRequestLength = sizeof(kRequest) - 1;

char s_Line[32];
size_t s_LineLength = 0;
bool s_LineTooLong = false;

const char* FormatName(Deki::ColorFormat format)
{
    switch (format)
    {
        case Deki::ColorFormat::RGB565: return "RGB565";
        case Deki::ColorFormat::RGB888: return "RGB888";
        case Deki::ColorFormat::ARGB8888: return "ARGB8888";
        case Deki::ColorFormat::RGB565A8: return "RGB565A8";
    }
    return "UNKNOWN";
}

void SendFrame()
{
    const Deki::Engine& engine = Deki::Engine::GetInstance();
    const uint8_t* frame = engine.GetFrameBuffer();
    const int32_t width = engine.GetScreenWidth();
    const int32_t height = engine.GetScreenHeight();
    if (!frame || width <= 0 || height <= 0)
    {
        std::printf("\nDEKI:SCREENSHOT:ERROR no frame has been drawn yet\n");
        std::fflush(stdout);
        return;
    }

    const Deki::ColorFormat format = engine.GetFrameBufferFormat();
    const size_t bytes = Deki::FrameBufferBytes(format, width, height);
    const uint32_t crc = esp_rom_crc32_le(0, frame, static_cast<uint32_t>(bytes));

    // A newline first: the request can arrive in the middle of a log line.
    std::printf("\nDEKI:SCREENSHOT:BEGIN %d %d %s %u\n", static_cast<int>(width), static_cast<int>(height),
                FormatName(format), static_cast<unsigned>(bytes));

    // Base64, 96 characters (72 bytes) a line: the console turns a newline
    // byte into CR LF, so the bytes cannot be sent as they are.
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char line[100];
    for (size_t at = 0; at < bytes; at += 72)
    {
        const size_t n = (bytes - at < 72) ? bytes - at : 72;
        size_t out = 0;
        for (size_t i = 0; i < n; i += 3)
        {
            const uint32_t b0 = frame[at + i];
            const uint32_t b1 = (i + 1 < n) ? frame[at + i + 1] : 0;
            const uint32_t b2 = (i + 2 < n) ? frame[at + i + 2] : 0;
            const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
            line[out++] = kAlphabet[(v >> 18) & 63];
            line[out++] = kAlphabet[(v >> 12) & 63];
            line[out++] = (i + 1 < n) ? kAlphabet[(v >> 6) & 63] : '=';
            line[out++] = (i + 2 < n) ? kAlphabet[v & 63] : '=';
        }
        line[out++] = '\n';
        std::fwrite(line, 1, out, stdout);
    }

    std::printf("DEKI:SCREENSHOT:END %08x\n", static_cast<unsigned>(crc));
    std::fflush(stdout);

    // Sending took about a second the game did not see go by: without this
    // the next frame is a second long, and everything timed jumps ahead.
    Deki::Time::ResetFrameTime();
}

void Feed(char c)
{
    if (c == '\n' || c == '\r')
    {
        if (!s_LineTooLong && s_LineLength == kRequestLength && std::memcmp(s_Line, kRequest, kRequestLength) == 0)
        {
            SendFrame();
        }
        s_LineLength = 0;
        s_LineTooLong = false;
        return;
    }
    if (s_LineLength < sizeof(s_Line))
    {
        s_Line[s_LineLength++] = c;
    }
    else
    {
        s_LineTooLong = true;
    }
}

// Once a frame, before it is drawn: the framebuffer still holds the frame on
// screen.
void Poll(uint32_t)
{
#if defined(DEKI_CAPTURE_USB_SERIAL_JTAG)
    while (usb_serial_jtag_ll_rxfifo_data_available())
    {
        uint8_t c = 0;
        if (usb_serial_jtag_ll_read_rxfifo(&c, 1) != 1)
        {
            break;
        }
        Feed(static_cast<char>(c));
    }
#else
    static bool s_NonBlocking = false;
    if (!s_NonBlocking)
    {
        const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        s_NonBlocking = true;
    }
    char buffer[32];
    ssize_t n = 0;
    while ((n = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0)
    {
        for (ssize_t i = 0; i < n; ++i)
        {
            Feed(buffer[i]);
        }
    }
#endif
}

}  // namespace

void StartScreenCapture()
{
    static bool s_Started = false;
    if (s_Started)
    {
        return;
    }
    s_Started = true;
    Deki::Engine::GetInstance().RegisterUpdate(&Poll);
}

}  // namespace DekiEsp32

#else  // !ESP32

namespace DekiEsp32
{
void StartScreenCapture()
{
}
}  // namespace DekiEsp32

#endif
