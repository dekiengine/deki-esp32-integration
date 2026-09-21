#include "ESP32PinSetup.h"
#include <deki/LogSystem.h>

#if defined(ESP32)
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace DekiEsp32
{

#if defined(ESP32)

static const char* TAG = "ESP32Pin";

void ESP32PinSetup::Setup(SetupCallback onComplete)
{
    if (pin < 0)
    {
        onComplete(true);
        return;
    }

    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio))
    {
        DEKI_LOG_ERROR("ESP32PinSetup: GPIO %d cannot be an output on this chip", (int)pin);
        onComplete(false);
        return;
    }

    // Level first, then direction: the pin never drives the wrong level.
    gpio_reset_pin(gpio);
    gpio_set_level(gpio, high ? 1 : 0);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    ESP_LOGI(TAG, "GPIO %d driven %s", (int)pin, high ? "high" : "low");

    if (settleMs > 0)
        vTaskDelay(pdMS_TO_TICKS(settleMs));

    onComplete(true);
}

#else

void ESP32PinSetup::Setup(SetupCallback onComplete)
{
    // No pins off the device
    onComplete(true);
}

#endif

}  // namespace DekiEsp32
