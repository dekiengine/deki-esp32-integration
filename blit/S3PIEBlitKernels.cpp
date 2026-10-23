// S3 PIE (Processor Instruction Extensions) SIMD blit kernels.
//
// Compiled only for the ESP32-S3: the whole file is guarded so other builds
// (desktop editor, P4, etc.) never see PIE-only intrinsics or instruction
// encodings.
//
// PIE reference: ESP32-S3 Technical Reference Manual, ch. "Processor
// Instruction Extensions", and the user-mode "ee.*" instruction set.

#include "S3PIEBlitKernels.h"

#if defined(__XTENSA__) && defined(CONFIG_IDF_TARGET_ESP32S3)

#include <cstring>

namespace DekiEsp32::Blit
{

// ---------------------------------------------------------------------------
// RGB565 1:1 row copy
// ---------------------------------------------------------------------------
// Copies pixelCount RGB565 pixels (2 bytes each) from src to dst with PIE
// 128-bit loads and stores. The caller guarantees both pointers are 16-byte
// aligned. Each iteration moves 8 pixels (16 bytes); memcpy copies the tail.
//
// Uses ee.vld.128.ip / ee.vst.128.ip, which post-increment the pointer by
// the immediate 16 (the "ip" suffix).

void S3PIERGB565CopyRow(const uint8_t* src, uint8_t* dst, int32_t pixelCount, uint8_t /*tintR*/, uint8_t /*tintG*/,
                        uint8_t /*tintB*/, uint8_t /*tintA*/)
{
    int32_t simdPairs = pixelCount >> 3;  // 8 pixels per 128-bit chunk
    int32_t tailPixels = pixelCount & 7;

    while (simdPairs > 0)
    {
        asm volatile("ee.vld.128.ip  q0, %[s], 16  \n"
                     "ee.vst.128.ip  q0, %[d], 16  \n"
                     : [s] "+r"(src), [d] "+r"(dst)
                     :
                     : "memory");
        --simdPairs;
    }

    if (tailPixels)
    {
        std::memcpy(dst, src, tailPixels * 2);
    }
}

}  // namespace DekiEsp32::Blit

#endif  // __XTENSA__ && CONFIG_IDF_TARGET_ESP32S3
