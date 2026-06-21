#pragma once

/**
 * @brief Serial command handler for ESP32 platforms
 *
 * Processes serial commands from the editor for device management:
 * - STORAGE_MODE: Enter USB storage mode for asset deployment
 * - EXIT_STORAGE: Exit storage mode and resume normal operation
 * - STATUS: Get current device status
 * - PING: Check if device is responsive
 */
class ESP32SerialCommands
{
   public:
    /**
     * @brief Initialize the serial command handler
     * @param baudRate Baud rate for serial communication (default: 115200)
     */
    static void Initialize(unsigned long baudRate = 115200);

    /**
     * @brief Process any pending serial commands
     * Call this from the main loop
     */
    static void ProcessCommands();

    /**
     * @brief Check if device is in storage mode
     * @return true if in storage mode
     */
    static bool IsInStorageMode();

   private:
    static bool s_Initialized;
    static bool s_InStorageMode;
};
