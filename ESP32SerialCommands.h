#pragma once

namespace DekiEsp32
{

/// Handles the editor's device-management commands over serial on ESP32:
/// - STORAGE_MODE: enter USB storage mode to deploy assets
/// - EXIT_STORAGE: leave storage mode and resume normal operation
/// - STATUS: report the device status
/// - PING: check that the device responds
class ESP32SerialCommands
{
public:
    /// Starts the command handler on the serial port at `baudRate`.
    static void Initialize(unsigned long baudRate = 115200);

    /// Handles pending serial commands. Call from the main loop.
    static void ProcessCommands();

    static bool IsInStorageMode();

private:
    static bool s_Initialized;
    static bool s_InStorageMode;
};

}  // namespace DekiEsp32
