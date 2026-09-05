#pragma once

#include "providers/IDekiPower.h"  // from deki-engine

#include <cstdint>
#include <vector>

/**
 * @brief ESP-IDF implementation of Deki::IPower.
 *
 * Light sleep only for this iteration. `SupportsMode(Deep)` returns false
 * until a follow-up wires NVS persistence and the reboot-as-wake handling
 * deep sleep requires (esp_deep_sleep_start never returns; the device boots
 * fresh when the wake source fires).
 *
 * Tick() drives the idle timer and, when the timeout elapses, fires
 * OnBeforeSleep callbacks, configures wake sources, and calls
 * esp_light_sleep_start() which blocks until wake. On wake the function
 * returns, OnScreenOn callbacks fire, and Tick() returns to the engine main
 * loop which proceeds with the next frame as normal.
 *
 * Backlight is toggled automatically in built-in OnBeforeSleep / OnScreenOn
 * handlers registered during Initialize(); app callbacks are appended to the
 * same lists and fire alongside the backlight ones.
 */
class ESPIDFPower : public Deki::IPower
{
public:
    ESPIDFPower()           = default;
    ~ESPIDFPower() override = default;

    bool Initialize() override;
    void Shutdown()   override;

    State     GetState() const            override { return m_State; }
    SleepInfo GetCurrentSleepInfo() const override { return m_LastSleepInfo; }
    bool      SupportsMode(SleepMode mode) const override;

    void NotifyActivity()                  override;
    void SetIdleTimeoutSec(int32_t s)      override { m_IdleTimeoutSec = s; }
    void SetIdleSleepMode(SleepMode mode)  override { m_IdleSleepMode  = mode; }
    void RequestSleep(SleepMode mode)      override;
    void SetWakeGpio(int gpio_num, int level) override;

    void RegisterOnScreenOn(SleepCallback cb)    override { m_OnScreenOn.push_back(std::move(cb)); }
    void RegisterOnBeforeSleep(SleepCallback cb) override { m_OnBeforeSleep.push_back(std::move(cb)); }

    void Tick() override;

private:
    void EnterSleep(SleepMode mode);
    void FireScreenOn(const SleepInfo& info);
    void FireBeforeSleep(const SleepInfo& info);

    State     m_State           = State::Awake;
    SleepInfo m_LastSleepInfo   = { SleepMode::Light, "boot" };
    SleepMode m_IdleSleepMode   = SleepMode::Light;
    int32_t   m_IdleTimeoutSec  = 0;          // 0 disables idle sleep
    int64_t   m_LastActivityUs  = 0;          // esp_timer_get_time() baseline

    int  m_WakeGpio  = -1;
    int  m_WakeLevel = 0;

    std::vector<SleepCallback> m_OnScreenOn;
    std::vector<SleepCallback> m_OnBeforeSleep;

    bool m_BootScreenOnFired = false;
};
