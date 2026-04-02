#include "ESP32SerialSetup.h"
#include "DekiLogSystem.h"
#include "DekiEngine.h"

#if defined(ESP32)

#include "platforms/esp32/ESP32SerialCommands.h"
#include "esp_log.h"
static const char* TAG = "ESP32Serial";

void ESP32SerialSetup::Setup(SetupCallback onComplete)
{
    ESP_LOGI(TAG, "Initializing serial at %d baud...", (int)baud_rate);

    ESP32SerialCommands::Initialize(static_cast<unsigned long>(baud_rate));

    DekiEngine::GetInstance().RegisterUpdate([](uint32_t) {
        ESP32SerialCommands::ProcessCommands();
    });

    ESP_LOGI(TAG, "Serial OK");
    onComplete(true);
}

#else

void ESP32SerialSetup::Setup(SetupCallback onComplete)
{
    // Non-ESP32: Serial commands not applicable
    onComplete(true);
}

#endif
