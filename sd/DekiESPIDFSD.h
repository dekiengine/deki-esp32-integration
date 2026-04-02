#pragma once

/**
 * @file DekiESPIDFSD.h
 * @brief ESP-IDF native SD card module for Deki engine
 *
 * Include this header to enable SD card support via ESP-IDF native SPI APIs.
 * Uses esp_vfs_fat_sdspi_mount for direct VFS integration.
 *
 * This implementation works on ESP32 platforms built with ESP-IDF:
 * - ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
 *
 * The module auto-registers via ESP32HALModule when building for ESP32.
 */

#include "ESPIDFSDCard.h"
#include "ESPIDFSDFileSystem.h"
