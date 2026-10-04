#pragma once

#include <deki/providers/IPower.h>  // from deki-engine

#include <cstdint>
#include <vector>

namespace DekiEsp32
{

/// Deki::IPower on ESP-IDF.
///
/// Light sleep only. `SupportsMode(Deep)` returns false: deep sleep needs NVS
/// persistence and reboot-as-wake handling (esp_deep_sleep_start never
/// returns; the device boots fresh when the wake source fires).
///
/// Tick() drives the idle timer. When the timeout passes it fires the
/// OnBeforeSleep callbacks, sets up the wake sources and calls
/// esp_light_sleep_start(), which blocks until wake. Then the OnScreenOn
/// callbacks fire and Tick() returns to the engine's main loop, which goes on
/// with the next frame.
///
/// Built-in OnBeforeSleep / OnScreenOn handlers registered in Initialize()
/// switch the backlight; app callbacks are appended to the same lists and
/// fire after them.
class ESPIDFPower : public Deki::IPower
{
public:
    ESPIDFPower() = default;
    ~ESPIDFPower() override = default;

    bool Initialize() override;
    void Shutdown() override;

    State GetState() const override { return m_State; }
    SleepInfo GetCurrentSleepInfo() const override { return m_LastSleepInfo; }
    bool SupportsMode(SleepMode mode) const override;

    void NotifyActivity() override;
    void SetIdleTimeoutSec(int32_t s) override { m_IdleTimeoutSec = s; }
    void SetIdleSleepMode(SleepMode mode) override { m_IdleSleepMode = mode; }
    void RequestSleep(SleepMode mode) override;
    void SetWakeGpio(int gpioNum, int level) override;

    void RegisterOnScreenOn(SleepCallback cb) override { m_OnScreenOn.push_back(std::move(cb)); }
    void RegisterOnBeforeSleep(SleepCallback cb) override { m_OnBeforeSleep.push_back(std::move(cb)); }

    void Tick() override;

private:
    void EnterSleep(SleepMode mode);
    void FireScreenOn(const SleepInfo& info);
    void FireBeforeSleep(const SleepInfo& info);

    State m_State = State::Awake;
    SleepInfo m_LastSleepInfo = { SleepMode::Light, "boot" };
    SleepMode m_IdleSleepMode = SleepMode::Light;
    int32_t m_IdleTimeoutSec = 0;  // 0 disables idle sleep
    int64_t m_LastActivityUs = 0;  // esp_timer_get_time() baseline

    int m_WakeGpio = -1;
    int m_WakeLevel = 0;

    std::vector<SleepCallback> m_OnScreenOn;
    std::vector<SleepCallback> m_OnBeforeSleep;

    bool m_BootScreenOnFired = false;
};

}  // namespace DekiEsp32
