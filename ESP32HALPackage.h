#pragma once

// Central header of the Deki ESP32 HAL package.
//
// The package holds the ESP-IDF backends and the ESP32 SetupComponents that
// boot.scene uses on device builds:
// - ESP32MemorySetup (PSRAM memory backend)
// - ESP32FileSystemSetup (LittleFS file system)
// - ESP32SerialSetup (serial command handler for talking to the editor)

#ifdef _WIN32
#ifdef DEKI_ESP32_HAL_EXPORTS
#define DEKI_ESP32_HAL_API __declspec(dllexport)
#else
#define DEKI_ESP32_HAL_API __declspec(dllimport)
#endif
#else
#define DEKI_ESP32_HAL_API __attribute__((visibility("default")))
#endif
