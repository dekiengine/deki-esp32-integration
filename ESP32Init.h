#pragma once

/// Starts what the ESP32 HAL runs alongside the game: the screen capture the
/// editor's device_screenshot asks for (ESP32ScreenCapture.h). Called from
/// DekiInitPackageSystems() once the engine is up.
///
/// Global scope on purpose: the editor generates a file that declares these as
/// plain `extern void DekiESP32HALInitSystem();`, and that file cannot know a
/// package's namespace (see deki-tween's TweenInit.h).
void DekiESP32HALInitSystem();
void DekiESP32HALShutdownSystem();
