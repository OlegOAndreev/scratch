#include "common.h"

#if defined(CPU_IS_X86_64)
#include <immintrin.h>
#elif defined(CPU_IS_AARCH64)
#include <arm_neon.h>
#endif
#include <stdint.h>

#if defined(WITH_IACA_SUPPORT)
#include <iacaMarks.h>
#else
#define IACA_START
#define IACA_END
#endif

// Returns the distance to the next aligned pointer after p.
template <size_t alignment>
FORCE_INLINE size_t toAlignPtr(char* p)
{
    return (UINTPTR_MAX - (uintptr_t)p + 1) & (alignment - 1);
}

#if defined(CPU_IS_X86_64)

using vector128_t = __m128;
using vector256_t = __m256;

FORCE_INLINE vector128_t load_v128(const char* p)
{
    return _mm_loadu_ps((const float*)p);
}

FORCE_INLINE void store_v128(char* p, vector128_t v)
{
    return _mm_storeu_ps((float*)p, v);
}

FORCE_INLINE void storea_v128(char* p, vector128_t v)
{
    return _mm_store_ps((float*)p, v);
}

FORCE_INLINE vector256_t load_v256(const char* p)
{
    return _mm256_loadu_ps((const float*)p);
}

FORCE_INLINE void store_v256(char* p, vector256_t v)
{
    return _mm256_storeu_ps((float*)p, v);
}

FORCE_INLINE void storea_v256(char* p, vector256_t v)
{
    return _mm256_store_ps((float*)p, v);
}

#elif defined(CPU_IS_AARCH64)

using vector128_t = int8x16_t;

FORCE_INLINE vector128_t load_v128(const char* p)
{
    return vld1q_s8((const int8_t*)p);
}

FORCE_INLINE void store_v128(char* p, vector128_t v)
{
    return vst1q_s8((int8_t*)p, v);
}

FORCE_INLINE void storea_v128(char* p, vector128_t v)
{
    return vst1q_s8((int8_t*)p, v);
}

#else

#error "Arch unsupported"

#endif

// A C++ reimplementation of naiveSseMemcpyUnrolledV2, including primitive support for ARM NEON.
void naiveSseOrNeonMemcpyUnrolledV2Cpp(char* dst, const char* src, size_t size)
{
    IACA_START
    if (size <= 32) {
        // Copy [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single load/stores with branches.
        if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            vector128_t x0 = load_v128(src);
            vector128_t x1 = load_v128(src + size - 16);
            store_v128(dst, x0);
            store_v128(dst + size - 16, x1);
        } else if (size > 8) {
            // Copy the first 8 bytes and the last 8 bytes (potentially overlapping).
            int64_t i0 = load_i64(src);
            int64_t i1 = load_i64(src + size - 8);
            store_i64(dst, i0);
            store_i64(dst + size - 8, i1);
        } else if (size > 4) {
            // Copy the first 4 bytes and the last 4 bytes (potentially overlapping).
            int32_t i0 = load_i32(src);
            int32_t i1 = load_i32(src + size - 4);
            store_i32(dst, i0);
            store_i32(dst + size - 4, i1);
        } else if (size == 4) {
            int32_t i0 = load_i32(src);
            store_i32(dst, i0);
        } else {
            if (size & 2) {
                int16_t i0 = load_i16(src + size - 2);
                store_i16(dst + size - 2, i0);
            }
            if (size & 1) {
                int8_t i0 = load_i16(src);
                store_i8(dst, i0);
            }
        }
    } else {
        // Align the dst on 16-byte
        size_t toAlignedDst = toAlignPtr<16>(dst);
        if (toAlignedDst != 0) {
            // We have at least 32 bytes, copy the first 16 one (including toAlignedDst bytes).
            vector128_t x0 = load_v128(src);
            src += toAlignedDst;
            store_v128(dst, x0);
            dst += toAlignedDst;
            size -= toAlignedDst;
        }

        // Load the last 32 bytes. Useful if size % 32 != 0.
        vector128_t xlast0 = load_v128(src + size - 32);
        vector128_t xlast1 = load_v128(src + size - 16);
        char* lastDst = dst + size - 32;

        // Main loop: do 32-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
        // bytes).
        for (size_t i = (size - 1) / 32; i != 0; i--) {
            vector128_t x0 = load_v128(src);
            vector128_t x1 = load_v128(src + 16);
            src += 32;
            storea_v128(dst, x0);
            storea_v128(dst + 16, x1);
            dst += 32;
        }

        // Store the last 32 bytes (potentially overlapping with the last iter).
        store_v128(lastDst, xlast0);
        store_v128(lastDst + 16, xlast1);
    }
    IACA_END
}

#if defined(CPU_IS_X86_64)

// A C++ reimplementation of naiveAvxMemcpyUnrolledV2.
void naiveAvxMemcpyUnrolledV2Cpp(char* dst, const char* src, size_t size)
{
    IACA_START
    if (size <= 64) {
        // Copy [33-64], [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single
        // load/stores with branches.
        if (size > 32) {
            vector256_t y0 = load_v256(src);
            vector256_t y1 = load_v256(src + size - 32);
            store_v256(dst, y0);
            store_v256(dst + size - 32, y1);
            _mm256_zeroupper();
        } else if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            vector128_t x0 = load_v128(src);
            vector128_t x1 = load_v128(src + size - 16);
            store_v128(dst, x0);
            store_v128(dst + size - 16, x1);
        } else if (size > 8) {
            // Copy the first 8 bytes and the last 8 bytes (potentially overlapping).
            int64_t i0 = load_i64(src);
            int64_t i1 = load_i64(src + size - 8);
            store_i64(dst, i0);
            store_i64(dst + size - 8, i1);
        } else if (size > 4) {
            // Copy the first 4 bytes and the last 4 bytes (potentially overlapping).
            int32_t i0 = load_i32(src);
            int32_t i1 = load_i32(src + size - 4);
            store_i32(dst, i0);
            store_i32(dst + size - 4, i1);
        } else if (size == 4) {
            int32_t i0 = load_i32(src);
            store_i32(dst, i0);
        } else {
            if (size & 2) {
                int16_t i0 = load_i16(src + size - 2);
                store_i16(dst + size - 2, i0);
            }
            if (size & 1) {
                int8_t i0 = load_i16(src);
                store_i8(dst, i0);
            }
        }
    } else {
        // Align the dst on 32-byte
        size_t toAlignedDst = toAlignPtr<32>(dst);
        if (toAlignedDst != 0) {
            // We have at least 64 bytes, copy the first 32 one (including toAlignedDst bytes).
            vector256_t y0 = load_v256(src);
            src += toAlignedDst;
            store_v256(dst, y0);
            dst += toAlignedDst;
            size -= toAlignedDst;
        }

        // Load the last 64 bytes. Useful if size % 64 != 0.
        vector256_t ylast0 = load_v256(src + size - 64);
        vector256_t ylast1 = load_v256(src + size - 32);
        char* lastDst = dst + size - 64;

        // Main loop: do 64-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
        // bytes).
        for (size_t i = (size - 1) / 64; i != 0; i--) {
            vector256_t y0 = load_v256(src);
            vector256_t y1 = load_v256(src + 32);
            src += 64;
            storea_v256(dst, y0);
            storea_v256(dst + 32, y1);
            dst += 64;
        }

        // Store the last 64 bytes (potentially overlapping with the last iter).
        store_v256(lastDst, ylast0);
        store_v256(lastDst + 32, ylast1);
        _mm256_zeroupper();
    }
    IACA_END
}

// A version of naiveAvxMemcpyUnrolledV2 with even more size cases.
void naiveAvxMemcpyUnrolledV3Cpp(char* dst, const char* src, size_t size)
{
    IACA_START
    if (size <= 64) {
        // Copy [33-64], [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single
        // load/stores with branches.
        if (size > 32) {
            vector256_t y0 = load_v256(src);
            vector256_t y1 = load_v256(src + size - 32);
            store_v256(dst, y0);
            store_v256(dst + size - 32, y1);
            _mm256_zeroupper();
        } else if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            vector128_t x0 = load_v128(src);
            vector128_t x1 = load_v128(src + size - 16);
            store_v128(dst, x0);
            store_v128(dst + size - 16, x1);
        } else if (size > 8) {
            // Copy the first 8 bytes and the last 8 bytes (potentially overlapping).
            int64_t i0 = load_i64(src);
            int64_t i1 = load_i64(src + size - 8);
            store_i64(dst, i0);
            store_i64(dst + size - 8, i1);
        } else if (size > 4) {
            // Copy the first 4 bytes and the last 4 bytes (potentially overlapping).
            int32_t i0 = load_i32(src);
            int32_t i1 = load_i32(src + size - 4);
            store_i32(dst, i0);
            store_i32(dst + size - 4, i1);
        } else if (size == 4) {
            int32_t i0 = load_i32(src);
            store_i32(dst, i0);
        } else {
            if (size & 2) {
                int16_t i0 = load_i16(src + size - 2);
                store_i16(dst + size - 2, i0);
            }
            if (size & 1) {
                int8_t i0 = load_i16(src);
                store_i8(dst, i0);
            }
        }
    } else {
        vector256_t ylast0 = load_v256(src + size - 64);
        vector256_t ylast1 = load_v256(src + size - 32);
        char* lastDst = dst + size - 64;

        if (size <= 512) {
            if (size >= 256) {
                vector256_t y0 = load_v256(src);
                vector256_t y1 = load_v256(src + 32);
                vector256_t y2 = load_v256(src + 64);
                vector256_t y3 = load_v256(src + 96);
                vector256_t y4 = load_v256(src + 128);
                vector256_t y5 = load_v256(src + 160);
                vector256_t y6 = load_v256(src + 192);
                vector256_t y7 = load_v256(src + 224);
                src += 256;
                store_v256(dst, y0);
                store_v256(dst + 32, y1);
                store_v256(dst + 64, y2);
                store_v256(dst + 96, y3);
                store_v256(dst + 128, y4);
                store_v256(dst + 160, y5);
                store_v256(dst + 192, y6);
                store_v256(dst + 224, y7);
                dst += 256;
                size -= 256;
            }
            if (size >= 128) {
                vector256_t y0 = load_v256(src);
                vector256_t y1 = load_v256(src + 32);
                vector256_t y2 = load_v256(src + 64);
                vector256_t y3 = load_v256(src + 96);
                src += 128;
                store_v256(dst, y0);
                store_v256(dst + 32, y1);
                store_v256(dst + 64, y2);
                store_v256(dst + 96, y3);
                dst += 128;
                size -= 128;
            }
            if (size >= 64) {
                vector256_t y0 = load_v256(src);
                vector256_t y1 = load_v256(src + 32);
                src += 64;
                store_v256(dst, y0);
                store_v256(dst + 32, y1);
                dst += 64;
                size -= 64;
            }
        } else {
            // Align the dst on 32-byte
            size_t toAlignedDst = toAlignPtr<32>(dst);
            if (toAlignedDst != 0) {
                // We have at least 64 bytes, copy the first 32 one (including toAlignedDst bytes).
                vector256_t y0 = load_v256(src);
                src += toAlignedDst;
                store_v256(dst, y0);
                dst += toAlignedDst;
                size -= toAlignedDst;
            }

            // Main loop: do 64-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
            // bytes).
            for (size_t i = (size - 1) / 64; i != 0; i--) {
                vector256_t y0 = load_v256(src);
                vector256_t y1 = load_v256(src + 32);
                src += 64;
                storea_v256(dst, y0);
                storea_v256(dst + 32, y1);
                dst += 64;
            }
        }

        // Store the last 64 bytes (potentially overlapping with the last iter).
        store_v256(lastDst, ylast0);
        store_v256(lastDst + 32, ylast1);
        _mm256_zeroupper();
    }
    IACA_END
}

#endif // CPU_IS_X86_64
