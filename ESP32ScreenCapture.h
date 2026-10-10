#pragma once

namespace DekiEsp32
{

/// Sends the frame on screen over the serial console when the editor asks,
/// for its device_screenshot tool: shows what the board really drew, which the
/// editor's play mode cannot.
///
/// The request is the line `DEKI:SCREENSHOT`. The answer is
///
///     DEKI:SCREENSHOT:BEGIN <width> <height> <format> <bytes>
///     <the framebuffer, base64, in lines>
///     DEKI:SCREENSHOT:END <crc32 of the bytes, 8 hex digits>
///
/// or `DEKI:SCREENSHOT:ERROR <reason>`. The bytes are the engine's framebuffer
/// as it sits in memory (RGB565 is little-endian), as last presented: the
/// request is answered from a per-frame update, before the frame is drawn.
///
/// The game pauses while the frame is sent (about a second for 320x240), and
/// that time is not counted as a frame: nothing timed jumps ahead.
///
/// Started by the package at boot; costs one FIFO poll a frame.
void StartScreenCapture();

}  // namespace DekiEsp32
