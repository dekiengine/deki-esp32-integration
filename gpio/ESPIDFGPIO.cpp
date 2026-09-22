#include "ESPIDFGPIO.h"
#include <deki/LogSystem.h>

#if defined(ESP32)
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#endif

namespace DekiEsp32
{

#if defined(ESP32)

static const char* TAG = "ESP32GPIO";

// One counter per pin, indexed by pin number. Written from interrupt context.
static volatile uint32_t s_EdgeCounts[GPIO_NUM_MAX];

static void IRAM_ATTR OnEdge(void* arg)
{
    const int pin = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    s_EdgeCounts[pin] = s_EdgeCounts[pin] + 1;
}

static bool ValidPin(int pin)
{
    return pin >= 0 && pin < GPIO_NUM_MAX && GPIO_IS_VALID_GPIO(pin);
}

static gpio_pull_mode_t ToPull(DekiGpio::Pull pull)
{
    switch (pull)
    {
        case DekiGpio::Pull::Up: return GPIO_PULLUP_ONLY;
        case DekiGpio::Pull::Down: return GPIO_PULLDOWN_ONLY;
        default: return GPIO_FLOATING;
    }
}

bool ESPIDFGPIO::Initialize()
{
    return true;
}

bool ESPIDFGPIO::SetOutput(int pin, bool high)
{
    if (!ValidPin(pin) || !GPIO_IS_VALID_OUTPUT_GPIO(pin))
        return false;
    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_reset_pin(gpio);
    gpio_set_level(gpio, high ? 1 : 0);
    return gpio_set_direction(gpio, GPIO_MODE_OUTPUT) == ESP_OK;
}

bool ESPIDFGPIO::SetInput(int pin, DekiGpio::Pull pull)
{
    if (!ValidPin(pin))
        return false;
    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    return gpio_set_pull_mode(gpio, ToPull(pull)) == ESP_OK;
}

bool ESPIDFGPIO::Write(int pin, bool high)
{
    if (!ValidPin(pin))
        return false;
    return gpio_set_level(static_cast<gpio_num_t>(pin), high ? 1 : 0) == ESP_OK;
}

bool ESPIDFGPIO::Read(int pin)
{
    if (!ValidPin(pin))
        return false;
    return gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
}

bool ESPIDFGPIO::CountEdges(int pin, DekiGpio::Edge edge, DekiGpio::Pull pull)
{
    if (!ValidPin(pin))
        return false;

    if (!m_IsrServiceInstalled)
    {
        // Another driver may have installed it already; that is not an error.
        const esp_err_t err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
            return false;
        }
        m_IsrServiceInstalled = true;
    }

    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = (pull == DekiGpio::Pull::Up) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = (pull == DekiGpio::Pull::Down) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    switch (edge)
    {
        case DekiGpio::Edge::Rising: cfg.intr_type = GPIO_INTR_POSEDGE; break;
        case DekiGpio::Edge::Falling: cfg.intr_type = GPIO_INTR_NEGEDGE; break;
        default: cfg.intr_type = GPIO_INTR_ANYEDGE; break;
    }
    if (gpio_config(&cfg) != ESP_OK)
        return false;

    s_EdgeCounts[pin] = 0;
    gpio_isr_handler_remove(gpio);
    const esp_err_t err = gpio_isr_handler_add(gpio, OnEdge, reinterpret_cast<void*>(static_cast<intptr_t>(pin)));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "gpio_isr_handler_add(%d): %s", pin, esp_err_to_name(err));
        return false;
    }
    return true;
}

uint32_t ESPIDFGPIO::TakeEdges(int pin)
{
    if (!ValidPin(pin))
        return 0;
    // Read then subtract rather than read then zero: an edge landing between
    // the two would be lost by a plain store of 0.
    const uint32_t count = s_EdgeCounts[pin];
    s_EdgeCounts[pin] = s_EdgeCounts[pin] - count;
    return count;
}

void ESPIDFGPIO::StopCounting(int pin)
{
    if (!ValidPin(pin))
        return;
    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_isr_handler_remove(gpio);
    gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
    s_EdgeCounts[pin] = 0;
}

#else

bool ESPIDFGPIO::Initialize() { return false; }
bool ESPIDFGPIO::SetOutput(int, bool) { return false; }
bool ESPIDFGPIO::SetInput(int, DekiGpio::Pull) { return false; }
bool ESPIDFGPIO::Write(int, bool) { return false; }
bool ESPIDFGPIO::Read(int) { return false; }
bool ESPIDFGPIO::CountEdges(int, DekiGpio::Edge, DekiGpio::Pull) { return false; }
uint32_t ESPIDFGPIO::TakeEdges(int) { return 0; }
void ESPIDFGPIO::StopCounting(int) {}

#endif

}  // namespace DekiEsp32
