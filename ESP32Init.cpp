#include "ESP32Init.h"
#include "ESP32ScreenCapture.h"

// Global scope, matching ESP32Init.h - see the comment there.
void DekiESP32HALInitSystem()
{
    DekiEsp32::StartScreenCapture();
}

void DekiESP32HALShutdownSystem()
{
}
