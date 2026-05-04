// S3 PIE (Processor Instruction Extensions) SIMD blit kernels.
//
// Compiled only when targeting the ESP32-S3 — the entire translation unit is
// guarded so non-S3 builds (desktop editor, P4, etc.) link cleanly with no
// references to PIE-only intrinsics or instruction encodings.
//
// PIE reference: ESP32-S3 Technical Reference Manual, ch. "Processor
// Instruction Extensions", and the user-mode "ee.*" instruction set.

#include "S3PIEBlitKernels.h"

#if defined(__XTENSA__) && defined(CONFIG_IDF_TARGET_ESP32S3)

#include <cstring>

namespace DekiESP32::Blit
{

// ---------------------------------------------------------------------------
// RGB565 1:1 row copy
// ---------------------------------------------------------------------------
// Copies pixelCount RGB565 pixels (2 bytes each) from src to dst using PIE
// 128-bit loads/stores. Caller guarantees both pointers are 16-byte aligned.
// Each iteration moves 8 pixels (16 bytes); tail is handled with memcpy.
//
// Uses ee.vld.128.ip / ee.vst.128.ip with post-increment by 16. The "ip"
// suffix produces an immediate post-increment; the assembler accepts an
// integer offset literal in the instruction encoding.

void S3PIE_RGB565_Copy_Row(const uint8_t* src, uint8_t* dst, int32_t pixelCount,
                           uint8_t /*tintR*/, uint8_t /*tintG*/,
                           uint8_t /*tintB*/, uint8_t /*tintA*/)
{
    int32_t simdPairs = pixelCount >> 3;        // 8 pixels per 128-bit chunk
    int32_t tailPixels = pixelCount & 7;

    while (simdPairs > 0)
    {
        asm volatile (
            "ee.vld.128.ip  q0, %[s], 16  \n"
            "ee.vst.128.ip  q0, %[d], 16  \n"
            : [s] "+r" (src), [d] "+r" (dst)
            :
            : "memory"
        );
        --simdPairs;
    }

    if (tailPixels)
        std::memcpy(dst, src, tailPixels * 2);
}

// ---------------------------------------------------------------------------
// RGB565A8 → RGB565 1:1 row alpha blend
// ---------------------------------------------------------------------------
// NOT YET REGISTERED. The per-pixel pipeline (3-byte gather, RGB565 unpack,
// multiply-add by alpha, repack) needs careful PIE asm with on-hardware bit
// verification. Until that exists, ESP32HALModule.cpp does not register this
// kernel — the QuadBlit dispatcher then runs its scalar inner loop, same as
// every other build target.
//
// The symbol is provided so the dispatch shape is end-to-end and future work
// only needs to fill in the body and flip the registration on.

void S3PIE_RGB565A8_Blend_Row(const uint8_t* /*src*/, uint8_t* /*dst*/,
                              int32_t /*pixelCount*/,
                              uint8_t /*tintR*/, uint8_t /*tintG*/,
                              uint8_t /*tintB*/, uint8_t /*tintA*/)
{
    // Intentionally empty until a verified PIE implementation lands.
    // This function is not registered with QuadBlit, so it is never called.
}

} // namespace DekiESP32::Blit

#endif // __XTENSA__ && CONFIG_IDF_TARGET_ESP32S3
