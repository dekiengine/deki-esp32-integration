#pragma once

/**
 * @file ESP32HALModule.h
 * @brief Central header for the Deki ESP32 HAL Module
 *
 * This module provides ESP32-specific SetupComponents for device builds:
 * - ESP32MemorySetup (configures PSRAM memory backend)
 * - ESP32FileSystemSetup (configures LittleFS file system)
 * - ESP32SerialSetup (configures serial command handler for editor communication)
 *
 * Backend implementations stay in platforms/esp32/ (ESP-IDF APIs).
 * This module provides SetupComponent wrappers for boot.prefab-driven initialization.
 */

// DLL export macro
#ifdef _WIN32
    #ifdef DEKI_ESP32_HAL_EXPORTS
        #define DEKI_ESP32_HAL_API __declspec(dllexport)
    #else
        #define DEKI_ESP32_HAL_API __declspec(dllimport)
    #endif
#else
    #define DEKI_ESP32_HAL_API __attribute__((visibility("default")))
#endif
