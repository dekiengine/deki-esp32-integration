#pragma once

#include "deki-rendering/QuadBlit.h"

// S3 PIE (Processor Instruction Extensions) SIMD kernels for the QuadBlit
// row dispatch table. S3PIEBlitKernels.cpp compiles them only for the
// ESP32-S3; on every other target it is empty and nothing refers to them.

namespace DekiEsp32::Blit
{

/// RGB565 1:1 row copy. src and dst point at RGB565 pixels (2 bytes each),
/// 16-byte aligned. pixelCount may be any non-negative number; the kernel
/// copies the tail without SIMD.
void S3PIERGB565CopyRow(const uint8_t* src, uint8_t* dst, int32_t pixelCount, uint8_t tintR, uint8_t tintG,
                        uint8_t tintB, uint8_t tintA);

/// RGB565A8 → RGB565 1:1 row alpha blend. src is 3 bytes per pixel (RGB565
/// plus alpha), dst 2 bytes per pixel, both 16-byte aligned. Skips a==0 and
/// copies a==255 per pixel. No tint and no chroma key: the caller has already
/// ruled those out.
void S3PIERGB565A8BlendRow(const uint8_t* src, uint8_t* dst, int32_t pixelCount, uint8_t tintR, uint8_t tintG,
                           uint8_t tintB, uint8_t tintA);

/// True if this build registers kernels, decided at compile time.
/// ESP32HALPackage.cpp uses it to skip registration on non-S3 builds.
constexpr bool HasS3PIEKernels()
{
#if defined(__XTENSA__) && defined(CONFIG_IDF_TARGET_ESP32S3)
    return true;
#else
    return false;
#endif
}

}  // namespace DekiEsp32::Blit
