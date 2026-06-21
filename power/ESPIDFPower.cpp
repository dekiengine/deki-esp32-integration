#include "ESPIDFPower.h"

#if defined(ESP32)

#include "DekiEngine.h"
#include "DekiLogSystem.h"
#include "providers/IDekiDisplay.h"

#include <esp_sleep.h>
#include <esp_timer.h>
#include <driver/gpio.h>

namespace
{
void SetBacklight(bool on)
{
    if (auto* d = DekiEngine::Instance().GetDisplay()) {
        d->SetBacklight(on);
    }
}
}  // namespace

bool ESPIDFPower::Initialize()
{
    m_LastActivityUs = esp_timer_get_time();

    // Built-in handlers: keep backlight in sync with sleep state. App callbacks
    // are appended to the same vectors and fire after these.
    m_OnBeforeSleep.push_back([](SleepInfo){ SetBacklight(false); });
    m_OnScreenOn.push_back   ([](SleepInfo){ SetBacklight(true);  });

    return true;
}

void ESPIDFPower::Shutdown()
{
    m_OnScreenOn.clear();
    m_OnBeforeSleep.clear();
}

bool ESPIDFPower::SupportsMode(SleepMode mode) const
{
    // Deep sleep needs NVS persistence + reboot-as-wake plumbing. Light only for now.
    return mode == SleepMode::Light;
}

void ESPIDFPower::NotifyActivity()
{
    m_LastActivityUs = esp_timer_get_time();
}

void ESPIDFPower::SetWakeGpio(int gpio_num, int level)
{
    m_WakeGpio  = gpio_num;
    m_WakeLevel = level ? 1 : 0;
}

void ESPIDFPower::RequestSleep(SleepMode mode)
{
    if (!SupportsMode(mode)) {
        DEKI_LOG_WARNING("ESPIDFPower: RequestSleep mode=%d not supported, ignoring",
                         static_cast<int>(mode));
        return;
    }
    EnterSleep(mode);
}

void ESPIDFPower::Tick()
{
    // First Tick after boot fires OnScreenOn so app code that needs to run on
    // every wake covers the cold-boot path with the same callback.
    if (!m_BootScreenOnFired) {
        m_BootScreenOnFired = true;
        FireScreenOn({ SleepMode::Light, "boot" });
    }

    if (m_IdleTimeoutSec <= 0) return;
    if (m_State != State::Awake) return;

    const int64_t now_us     = esp_timer_get_time();
    const int64_t timeout_us = (int64_t)m_IdleTimeoutSec * 1000000;
    if (now_us - m_LastActivityUs < timeout_us) return;

    EnterSleep(m_IdleSleepMode);
}

void ESPIDFPower::EnterSleep(SleepMode mode)
{
    const SleepInfo info = {
        mode,
        mode == SleepMode::Light ? "esp_light_sleep" : "esp_deep_sleep",
    };
    m_LastSleepInfo = info;

    FireBeforeSleep(info);

    if (m_WakeGpio >= 0) {
        esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(m_WakeGpio), m_WakeLevel);
    }

    m_State = State::Sleeping;

    if (mode == SleepMode::Light) {
        // Blocks until wake source fires.
        esp_light_sleep_start();
    } else {
        // Never returns; device reboots on wake.
        esp_deep_sleep_start();
    }

    m_State          = State::Awake;
    m_LastActivityUs = esp_timer_get_time();

    FireScreenOn(info);
}

void ESPIDFPower::FireScreenOn(const SleepInfo& info)
{
    for (auto& cb : m_OnScreenOn) {
        if (cb) cb(info);
    }
}

void ESPIDFPower::FireBeforeSleep(const SleepInfo& info)
{
    for (auto& cb : m_OnBeforeSleep) {
        if (cb) cb(info);
    }
}

#else  // !ESP32

bool ESPIDFPower::Initialize()                         { return false; }
void ESPIDFPower::Shutdown()                           {}
bool ESPIDFPower::SupportsMode(SleepMode) const        { return false; }
void ESPIDFPower::NotifyActivity()                     {}
void ESPIDFPower::SetWakeGpio(int, int)                {}
void ESPIDFPower::RequestSleep(SleepMode)              {}
void ESPIDFPower::Tick()                               {}
void ESPIDFPower::EnterSleep(SleepMode)                {}
void ESPIDFPower::FireScreenOn(const SleepInfo&)       {}
void ESPIDFPower::FireBeforeSleep(const SleepInfo&)    {}

#endif  // ESP32
