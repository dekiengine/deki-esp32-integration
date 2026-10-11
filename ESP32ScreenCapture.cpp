#include "ESP32ScreenCapture.h"

#if defined(ESP32)

#include <deki/Engine.h>
#include <deki/Scene.h>
#include <deki/Time.h>
#include <deki/assets/AssetManager.h>
#include <deki/reflection/ComponentRegistry.h>

#include "RenderProfiler.h"  // from deki-rendering

#include <string>
#include <vector>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
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
constexpr char kStatsRequest[] = "DEKI:STATS";
constexpr size_t kStatsRequestLength = sizeof(kStatsRequest) - 1;
constexpr char kProfileRequest[] = "DEKI:PROFILE";
constexpr size_t kProfileRequestLength = sizeof(kProfileRequest) - 1;
constexpr char kSceneRequest[] = "DEKI:SCENE ";
constexpr size_t kSceneRequestLength = sizeof(kSceneRequest) - 1;

// Frame statistics over windows of about a second: how many frames, how long
// they took from one update to the next, and the engine's split of each.
struct FrameWindow
{
    uint32_t frames = 0;
    int64_t frameUs = 0;
    uint32_t updateMs = 0;
    uint32_t renderMs = 0;
    uint32_t presentMs = 0;
};
FrameWindow s_Window;
FrameWindow s_LastWindow;  // the last complete one, for DEKI:STATS
int64_t s_WindowStartUs = 0;
int64_t s_LastPollUs = 0;

// "DEKI:SCENE " and a GUID is the longest request.
char s_Line[64];
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

void SendStats()
{
    const FrameWindow& w = s_LastWindow;
    if (w.frames == 0)
    {
        std::printf("\nDEKI:STATS:ERROR no frames counted yet\n");
        std::fflush(stdout);
        return;
    }
    const double frames = static_cast<double>(w.frames);
    const double frameMs = static_cast<double>(w.frameUs) / 1000.0 / frames;
    std::printf("\nDEKI:STATS fps=%.1f frameMs=%.2f updateMs=%.2f renderMs=%.2f presentMs=%.2f frames=%u "
                "internalFree=%u externalFree=%u\n",
                frameMs > 0.0 ? 1000.0 / frameMs : 0.0, frameMs, w.updateMs / frames, w.renderMs / frames,
                w.presentMs / frames, static_cast<unsigned>(w.frames),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    std::fflush(stdout);
}

// Counts the frame that just ended. Called once a frame, from Poll.
void CountFrame()
{
    const int64_t now = esp_timer_get_time();
    if (s_LastPollUs != 0)
    {
        const Deki::Engine::FrameTimings t = Deki::Engine::GetInstance().GetLastFrameTimings();
        ++s_Window.frames;
        s_Window.frameUs += now - s_LastPollUs;
        s_Window.updateMs += t.update;
        s_Window.renderMs += t.render;
        s_Window.presentMs += t.present;
    }
    else
    {
        s_WindowStartUs = now;
    }
    s_LastPollUs = now;
    if (now - s_WindowStartUs >= 1000000 && s_Window.frames > 0)
    {
        s_LastWindow = s_Window;
        s_Window = FrameWindow{};
        s_WindowStartUs = now;
    }
}

// After a pause that is not the game's (sending a screenshot), counting starts
// afresh, so the pause is not taken for a slow frame.
void RestartCount()
{
    s_Window = FrameWindow{};
    s_LastPollUs = 0;
}

// Opens the scene asset `guid` as the game's scene, as a scene link would:
// for measuring each scene without someone there to press its button.
void OpenScene(const std::string& guid)
{
    auto* assets = Deki::AssetManager::Get();
    auto* scene = assets ? static_cast<Deki::Scene*>(assets->LoadByGuidAndType(guid, Deki::Scene::kAssetTypeName))
                         : nullptr;
    if (!scene)
    {
        std::printf("\nDEKI:SCENE:ERROR no scene %s\n", guid.c_str());
        std::fflush(stdout);
        return;
    }
    Deki::Engine::GetInstance().SetRootScene(scene, true);
    std::printf("\nDEKI:SCENE:OK %s\n", guid.c_str());
    std::fflush(stdout);
}

uint64_t MicrosNow()
{
    return static_cast<uint64_t>(esp_timer_get_time());
}

// The first request turns the render profiler on; each one after reports
// what it measured since the one before, per frame, and starts again.
void SendProfile()
{
    namespace Profiler = DekiRendering::RenderProfiler;
    if (!Profiler::IsEnabled())
    {
        Profiler::Enable(&MicrosNow);
        std::printf("\nDEKI:PROFILE:ON\n");
        std::fflush(stdout);
        return;
    }
    uint32_t frames = 0;
    const std::vector<Profiler::Entry> entries = Profiler::Take(frames);
    std::printf("\nDEKI:PROFILE frames=%u\n", static_cast<unsigned>(frames));
    const double perFrame = frames > 0 ? 1.0 / frames : 0.0;
    for (const Profiler::Entry& e : entries)
    {
        const char* name = "?";
        if (e.typeId == Profiler::kClear)
        {
            name = "Clear";
        }
        else if (const Deki::ComponentMeta* meta = Deki::ComponentRegistry::Instance().GetMeta(e.typeId))
        {
            name = meta->name;
        }
        std::printf("DEKI:PROFILE:ITEM %s draws=%.1f contentUs=%.0f blitUs=%.0f pixels=%.0f\n", name,
                    e.draws * perFrame, e.contentUs * perFrame, e.blitUs * perFrame, e.pixels * perFrame);
    }
    std::printf("DEKI:PROFILE:END\n");
    std::fflush(stdout);
}

bool LineIs(const char* request, size_t length)
{
    return !s_LineTooLong && s_LineLength == length && std::memcmp(s_Line, request, length) == 0;
}

void Feed(char c)
{
    if (c == '\n' || c == '\r')
    {
        if (LineIs(kRequest, kRequestLength))
        {
            SendFrame();
            RestartCount();
        }
        else if (LineIs(kStatsRequest, kStatsRequestLength))
        {
            SendStats();
        }
        else if (LineIs(kProfileRequest, kProfileRequestLength))
        {
            SendProfile();
        }
        else if (!s_LineTooLong && s_LineLength > kSceneRequestLength &&
                 std::memcmp(s_Line, kSceneRequest, kSceneRequestLength) == 0)
        {
            OpenScene(std::string(s_Line + kSceneRequestLength, s_LineLength - kSceneRequestLength));
            RestartCount();
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
    CountFrame();
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
