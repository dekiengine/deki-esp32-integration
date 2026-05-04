#pragma once

#include "deki-rendering/QuadBlit.h"

// S3 PIE (Processor Instruction Extensions) SIMD kernels for the QuadBlit
// row dispatch table. Implementations live in S3PIEBlitKernels.cpp and are
// only compiled when targeting the ESP32-S3; on every other build target the
// .cpp file expands to nothing and these symbols are not referenced.

namespace DekiESP32::Blit
{

// RGB565 1:1 row copy. src/dst point at RGB565 pixels (2 bytes/pixel),
// 16-byte aligned. pixelCount may be any non-negative integer; tail handled
// scalar inside the kernel.
void S3PIE_RGB565_Copy_Row(const uint8_t* src, uint8_t* dst, int32_t pixelCount,
                           uint8_t tintR, uint8_t tintG, uint8_t tintB, uint8_t tintA);

// RGB565A8 → RGB565 1:1 row alpha blend. src is 3 bytes/pixel (RGB565 + 1
// alpha), dst is 2 bytes/pixel. Both 16-byte aligned. Handles a==0 skip and
// a==255 short-circuit per pixel. No tint, no chroma key — caller has already
// gated those out.
void S3PIE_RGB565A8_Blend_Row(const uint8_t* src, uint8_t* dst, int32_t pixelCount,
                              uint8_t tintR, uint8_t tintG, uint8_t tintB, uint8_t tintA);

// True if this build will register kernels (compile-time gate). Used by
// ESP32HALModule.cpp to suppress the registration call on non-S3 builds.
constexpr bool HasS3PIEKernels()
{
#if defined(__XTENSA__) && defined(CONFIG_IDF_TARGET_ESP32S3)
    return true;
#else
    return false;
#endif
}

} // namespace DekiESP32::Blit
