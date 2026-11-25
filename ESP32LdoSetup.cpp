#include "ESP32LdoSetup.h"

#include <deki/LogSystem.h>

#if defined(ESP32) && !defined(DEKI_EDITOR)
#include <esp_err.h>
#include <soc/soc_caps.h>
#if SOC_GP_LDO_SUPPORTED
#include <esp_ldo_regulator.h>
#endif
#endif

namespace DekiEsp32
{

#if defined(ESP32) && !defined(DEKI_EDITOR)

void ESP32LdoSetup::Setup(SetupCallback onComplete)
{
#if SOC_GP_LDO_SUPPORTED
    esp_ldo_channel_config_t config = {};
    config.chan_id = channel;
    config.voltage_mv = millivolts;
    // Kept for the program's life, like the rest the boot scene starts.
    esp_ldo_channel_handle_t handle = nullptr;
    const esp_err_t err = esp_ldo_acquire_channel(&config, &handle);
    if (err != ESP_OK)
    {
        DEKI_LOG_ERROR("ESP32LdoSetup: LDO channel %d at %d mV did not turn on (%s)", static_cast<int>(channel),
                       static_cast<int>(millivolts), esp_err_to_name(err));
        onComplete(false);
        return;
    }
    DEKI_LOG_INFO("ESP32LdoSetup: LDO channel %d on at %d mV", static_cast<int>(channel),
                  static_cast<int>(millivolts));
    onComplete(true);
#else
    DEKI_LOG_ERROR("ESP32LdoSetup: this chip has no LDO regulators to turn on (the ESP32-P4 has)");
    onComplete(false);
#endif
}

#else

void ESP32LdoSetup::Setup(SetupCallback onComplete)
{
    // The editor has no board rails to power.
    onComplete(true);
}

#endif

}  // namespace DekiEsp32
