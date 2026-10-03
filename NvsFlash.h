#pragma once

// Non-volatile storage, which the WiFi and Bluetooth stacks keep their
// calibration and keys in. esp_wifi_init fails with ESP_ERR_NVS_NOT_INITIALIZED
// until it is initialised, and nothing in Deki did, so WiFi never connected.

#if defined(ESP32)
#include "esp_err.h"
#include "nvs_flash.h"
#include <deki/LogSystem.h>

namespace DekiEsp32
{

// Initialise NVS once. A partition that is full or was written by a newer
// IDF is erased and initialised again, as ESP-IDF's own examples do.
inline bool EnsureNvsFlash()
{
    static bool s_Ready = false;
    if (s_Ready)
        return true;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        DEKI_LOG_WARNING("[nvs] partition unusable (%d); erasing it", e);
        if (nvs_flash_erase() == ESP_OK)
            e = nvs_flash_init();
    }
    if (e != ESP_OK)
    {
        DEKI_LOG_ERROR("[nvs] nvs_flash_init failed (%d)", e);
        return false;
    }
    s_Ready = true;
    return true;
}

}  // namespace DekiEsp32
#endif
