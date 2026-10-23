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

#include <deki/Engine.h>  // kRGB565BigEndian
#include "esp_attr.h"

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

// ---------------------------------------------------------------------------
// RGB565A8 → RGB565 1:1 row alpha blend, tinted
// ---------------------------------------------------------------------------
// The pixels QuadBlit's scalar blend writes (TintPixel565, CompositePixel),
// bit for bit, eight at a time in 16-bit lanes. ee.vmul.u16 gives
// ((x * y) >> SAR) & 0xFFFF, which on hardware holds for any product, so:
//
//   a    = Div255(alpha * tintA)
//   S8   = Div255(c5 * 8 * tint), Div255(c6 * 4 * tint)   the tinted channel
//   out5 = Div255((S8 * a) >> 3 + d5 * (255 - a))         red, blue
//   out6 = Div255((S8 * a) >> 2 + d6 * (255 - a))         green
//   Div255(x) = ((x + 1) * 257) >> 16
//
// The scalar code blends in 8 bits and packs to 5/6; because the
// destination's 8-bit channel is d5 << 3 (d6 << 2), the packed result is the
// line above exactly. Div255's form holds for x up to 65280. Every sum stays
// below 16257 and no value 1 is added to is 32767, so the saturating adds
// never saturate. a == 0 gives the destination back and a == 255 the tinted
// source, so no pixel needs a branch. Checked against the scalar code on an
// ESP32-S3 over every tint and alpha tint, row lengths 1 to 40: no
// difference. NOT REGISTERED (ESP32HALPackage.cpp): alone it blends a row of
// pixels in half the time of a plain scalar loop, but in a frame QuadBlit's
// own loop (tint tables, clear pixels skipped after one read) was faster,
// since gathering eight 3-byte pixels costs about what the eight-wide maths
// saves. Pixels stored with colour and alpha in separate planes would let it
// load them directly.

namespace
{
// The constants, in the order the kernel reads them (ee.vldbc.16.ip walks
// the table).
constexpr int kBlendConsts = 25;

void MakeBlendConsts(uint16_t* k, uint8_t tr, uint8_t tg, uint8_t tb, uint8_t ta)
{
    const uint16_t list[kBlendConsts] = {
        ta, 1, 257, 255,                                          // alpha
        1, static_cast<uint16_t>(8 * tr), 1, 257, 1, 257, 2048,   // red
        1, 63, static_cast<uint16_t>(4 * tg), 1, 257, 1, 257, 32, // green
        31, static_cast<uint16_t>(8 * tb), 1, 257, 1, 257,        // blue
    };
    std::memcpy(k, list, sizeof(list));
}

// Eight pixels: C the source colours and A their alphas, D the destination,
// all in the CPU's 565 order; O gets the result. All 16-byte aligned.
IRAM_ATTR void Blend8(const uint16_t* C, const uint16_t* A, const uint16_t* D, uint16_t* O, const uint16_t* k)
{
    asm volatile(
        "ee.vld.128.ip   q0, %[c], 0     \n"
        "ee.vld.128.ip   q1, %[a], 0     \n"
        "ee.vld.128.ip   q2, %[d], 0     \n"
        // a = Div255(alpha * ta); inv = 255 - a
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // ta
        "ssai 0                           \n"
        "ee.vmul.u16     q1, q1, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 1
        "ee.vadds.s16    q1, q1, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 257
        "ssai 16                          \n"
        "ee.vmul.u16     q1, q1, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 255
        "ee.vsubs.s16    q4, q3, q1      \n"
        // ---- red: r5 = C >> 11, d5 = D >> 11
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 1
        "ssai 11                          \n"
        "ee.vmul.u16     q5, q0, q3      \n"
        "ee.vmul.u16     q6, q2, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 8 * tr
        "ssai 0                           \n"
        "ee.vmul.u16     q5, q5, q3      \n"
        "ee.vmul.u16     q6, q6, q4      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 1
        "ee.vadds.s16    q5, q5, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 257
        "ssai 16                          \n"
        "ee.vmul.u16     q5, q5, q3      \n"  // S8
        "ssai 3                           \n"
        "ee.vmul.u16     q5, q5, q1      \n"  // (S8 * a) >> 3
        "ee.vadds.s16    q5, q5, q6      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 1
        "ee.vadds.s16    q5, q5, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 257
        "ssai 16                          \n"
        "ee.vmul.u16     q5, q5, q3      \n"  // R5
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 2048
        "ssai 0                           \n"
        "ee.vmul.u16     q7, q5, q3      \n"
        // ---- green: g6 = (C >> 5) & 63, d6 = (D >> 5) & 63
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 1
        "ssai 5                           \n"
        "ee.vmul.u16     q5, q0, q3      \n"
        "ee.vmul.u16     q6, q2, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 63
        "ee.andq         q5, q5, q3      \n"
        "ee.andq         q6, q6, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 4 * tg
        "ssai 0                           \n"
        "ee.vmul.u16     q5, q5, q3      \n"
        "ee.vmul.u16     q6, q6, q4      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 1
        "ee.vadds.s16    q5, q5, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 257
        "ssai 16                          \n"
        "ee.vmul.u16     q5, q5, q3      \n"  // S8
        "ssai 2                           \n"
        "ee.vmul.u16     q5, q5, q1      \n"  // (S8 * a) >> 2
        "ee.vadds.s16    q5, q5, q6      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 1
        "ee.vadds.s16    q5, q5, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 257
        "ssai 16                          \n"
        "ee.vmul.u16     q5, q5, q3      \n"  // G6
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 32
        "ssai 0                           \n"
        "ee.vmul.u16     q5, q5, q3      \n"
        "ee.orq          q7, q7, q5      \n"
        // ---- blue: b5 = C & 31, d5 = D & 31
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 31
        "ee.andq         q5, q0, q3      \n"
        "ee.andq         q6, q2, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 8 * tb
        "ee.vmul.u16     q5, q5, q3      \n"
        "ee.vmul.u16     q6, q6, q4      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 1
        "ee.vadds.s16    q5, q5, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 257
        "ssai 16                          \n"
        "ee.vmul.u16     q5, q5, q3      \n"  // S8
        "ssai 3                           \n"
        "ee.vmul.u16     q5, q5, q1      \n"  // (S8 * a) >> 3
        "ee.vadds.s16    q5, q5, q6      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 1
        "ee.vadds.s16    q5, q5, q3      \n"
        "ee.vldbc.16.ip  q3, %[k], 2     \n"  // 257
        "ssai 16                          \n"
        "ee.vmul.u16     q5, q5, q3      \n"  // B5
        "ee.orq          q7, q7, q5      \n"
        "ee.vst.128.ip   q7, %[o], 0     \n"
        : [k] "+r"(k)
        : [c] "r"(C), [a] "r"(A), [d] "r"(D), [o] "r"(O)
        : "memory");
}

// A pixel as stored (memory's byte order) to the CPU's 565 order, and back:
// a byte swap is its own inverse.
inline uint16_t Cpu565(uint16_t stored)
{
    if constexpr (Deki::kRGB565BigEndian)
    {
        return static_cast<uint16_t>((stored >> 8) | (stored << 8));
    }
    return stored;
}

// One pixel, the same formula one lane at a time: for chunks with too few
// pixels to blend to pay for the eight-wide path (gathering eight pixels and
// their destinations costs about what blending three or four does).
inline uint32_t Div255(uint32_t x)
{
    return (x + 1 + (x >> 8)) >> 8;
}

inline void BlendOne(const uint8_t* sp, uint16_t* dp, uint8_t tr, uint8_t tg, uint8_t tb, uint8_t ta)
{
    uint32_t a = sp[2];
    if (ta != 255)
    {
        a = Div255(a * ta);
    }
    if (a == 0)
    {
        return;
    }
    const uint32_t v = Cpu565(static_cast<uint16_t>(sp[0] | (sp[1] << 8)));
    const uint32_t r = Div255((v >> 11) * 8u * tr);
    const uint32_t g = Div255(((v >> 5) & 63) * 4u * tg);
    const uint32_t b = Div255((v & 31) * 8u * tb);
    if (a == 255)
    {
        *dp = Cpu565(static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
        return;
    }
    const uint32_t bg = Cpu565(*dp);
    const uint32_t inv = 255 - a;
    const uint32_t r5 = Div255(((r * a) >> 3) + (bg >> 11) * inv);
    const uint32_t g6 = Div255(((g * a) >> 2) + ((bg >> 5) & 63) * inv);
    const uint32_t b5 = Div255(((b * a) >> 3) + (bg & 31) * inv);
    *dp = Cpu565(static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5));
}
// Eight pixels (m of them real, for a row's tail) at sp onto dp: one pass
// gathers them and sorts the chunk. All clear: nothing to do. All opaque with
// no tint: a copy. Five or more clear: the few left one at a time, which
// costs less than gathering eight for the vector path.
template <bool Whole>
IRAM_ATTR inline void BlendChunk(const uint8_t* sp, uint16_t* dp, int32_t m, bool plain, const uint16_t* k, uint8_t tr,
                         uint8_t tg, uint8_t tb, uint8_t ta)
{
    alignas(16) uint16_t C[8], A[8], D[8], O[8];
    uint32_t anyA = 0, allA = 255, clear = 0;
    for (int32_t j = 0; j < 8; j++)
    {
        uint32_t a = 0;
        uint32_t c = 0;
        if (Whole || j < m)
        {
            c = static_cast<uint32_t>(sp[j * 3] | (sp[j * 3 + 1] << 8));
            a = sp[j * 3 + 2];
            allA &= a;
        }
        C[j] = static_cast<uint16_t>(c);
        A[j] = static_cast<uint16_t>(a);
        anyA |= a;
        clear += a == 0 ? 1u : 0u;
    }
    if (anyA == 0)
    {
        return;
    }
    if (plain && allA == 255)
    {
        for (int32_t j = 0; j < m; j++)
        {
            dp[j] = C[j];  // the source as stored
        }
        return;
    }
    if (clear >= 5)
    {
        for (int32_t j = 0; j < m; j++)
        {
            BlendOne(sp + j * 3, dp + j, tr, tg, tb, ta);
        }
        return;
    }
    for (int32_t j = 0; j < 8; j++)
    {
        C[j] = Cpu565(C[j]);
        D[j] = (Whole || j < m) ? Cpu565(dp[j]) : 0;
    }
    Blend8(C, A, D, O, k);
    for (int32_t j = 0; j < m; j++)
    {
        dp[j] = Cpu565(O[j]);
    }
}
}  // namespace

IRAM_ATTR void S3PIERGB565A8BlendRow(const uint8_t* src, uint8_t* dst8, int32_t pixelCount, uint8_t tintR,
                                     uint8_t tintG, uint8_t tintB, uint8_t tintA)
{
    if (pixelCount <= 0)
    {
        return;
    }
    // RGB565 rows are 2-byte aligned; the source's pixels are 3 bytes apart.
    uint16_t* d = reinterpret_cast<uint16_t*>(dst8);
    const bool plain = (tintR & tintG & tintB & tintA) == 255;
    alignas(16) uint16_t k[kBlendConsts + 7];
    MakeBlendConsts(k, tintR, tintG, tintB, tintA);
    int32_t i = 0;
    for (; i + 8 <= pixelCount; i += 8, src += 24)
    {
        BlendChunk<true>(src, d + i, 8, plain, k, tintR, tintG, tintB, tintA);
    }
    if (i < pixelCount)
    {
        BlendChunk<false>(src, d + i, pixelCount - i, plain, k, tintR, tintG, tintB, tintA);
    }
}

}  // namespace DekiEsp32::Blit

#endif  // __XTENSA__ && CONFIG_IDF_TARGET_ESP32S3
