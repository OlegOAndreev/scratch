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

// A version of naiveSseMemcpyUnrolledAligned, but with regular integer loads (and 8-byte alignment instead of 16-byte).
void naiveMemcpyUnrolledAlignedCpp(char* dst, const char* src, size_t size)
{
    if (size >= 32) {
        // Align the dst on 8-byte.
        size_t toAlignedDst = toAlignPtr<8>(dst);
        // Copy 4, 2, 1 bytes depending on set bits.
        if (toAlignedDst & 1) {
            int8_t x0 = load_i8(src);
            src += 1;
            store_i8(dst, x0);
            dst += 1;
        }
        if (toAlignedDst & 2) {
            int16_t x0 = load_i16(src);
            src += 2;
            store_i16(dst, x0);
            dst += 2;
        }
        if (toAlignedDst & 4) {
            int32_t x0 = load_i32(src);
            src += 4;
            store_i32(dst, x0);
            dst += 4;
        }
        size -= toAlignedDst;

        // Main loop: do 32-byte iters.
        for (size_t i = size / 32; i != 0; i--) {
            int64_t x0 = load_i64(src);
            int64_t x1 = load_i64(src + 8);
            int64_t x2 = load_i64(src + 16);
            int64_t x3 = load_i64(src + 24);
            src += 32;
            store_i64(dst, x0);
            store_i64(dst + 8, x1);
            store_i64(dst + 16, x2);
            store_i64(dst + 24, x3);
            dst += 32;
        }
        size &= 31;
    }
    // Copy 16, 8, 4, 2, 1 bytes depending on set bits.
    if (size & 1) {
        int8_t x0 = load_i8(src);
        src += 1;
        store_i8(dst, x0);
        dst += 1;
    }
    if (size & 2) {
        int16_t x0 = load_i16(src);
        src += 2;
        store_i16(dst, x0);
        dst += 2;
    }
    if (size & 4) {
        int32_t x0 = load_i32(src);
        src += 4;
        store_i32(dst, x0);
        dst += 4;
    }
    if (size & 8) {
        int64_t x0 = load_i64(src);
        src += 8;
        store_i64(dst, x0);
        dst += 8;
    }
    if (size & 16) {
        int64_t x0 = load_i64(src);
        int64_t x1 = load_i64(src + 8);
        src += 16;
        store_i64(dst, x0);
        store_i64(dst + 8, x1);
        dst += 16;
    }
}

// A version of naiveSseMemcpyUnrolledAlignedV2, but with regular integer loads (and 8-byte alignment instead of 16-byte).
void naiveMemcpyUnrolledAlignedV2Cpp(char* dst, const char* src, size_t size)
{
    if (size <= 32) {
        // Copy [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single load/stores with branches.
        if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            int64_t x0 = load_i64(src);
            int64_t x1 = load_i64(src + 8);
            int64_t x2 = load_i64(src + size - 16);
            int64_t x3 = load_i64(src + size - 8);
            store_i64(dst, x0);
            store_i64(dst + 8, x1);
            store_i64(dst + size - 16, x2);
            store_i64(dst + size - 8, x3);
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
        // Align the dst on 8-byte.
        size_t toAlignedDst = toAlignPtr<8>(dst);
        if (toAlignedDst != 0) {
            // We have at least 32 bytes, copy the first 8 one (including toAlignedDst bytes).
            int64_t x0 = load_i64(src);
            src += toAlignedDst;
            store_i64(dst, x0);
            dst += toAlignedDst;
            size -= toAlignedDst;
        }

        // Load the last 32 bytes. Useful if size % 32 != 0.
        int64_t xlast0 = load_i64(src + size - 32);
        int64_t xlast1 = load_i64(src + size - 24);
        int64_t xlast2 = load_i64(src + size - 16);
        int64_t xlast3 = load_i64(src + size - 8);
        char* lastDst = dst + size - 32;

        // Main loop: do 32-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
        // bytes).
        for (size_t i = (size - 1) / 32; i != 0; i--) {
            int64_t x0 = load_i64(src);
            int64_t x1 = load_i64(src + 8);
            int64_t x2 = load_i64(src + 16);
            int64_t x3 = load_i64(src + 24);
            src += 32;
            store_i64(dst, x0);
            store_i64(dst + 8, x1);
            store_i64(dst + 16, x2);
            store_i64(dst + 24, x3);
            dst += 32;
        }

        // Store the last 32 bytes (potentially overlapping with the last iter).
        store_i64(lastDst, xlast0);
        store_i64(lastDst + 8, xlast1);
        store_i64(lastDst + 16, xlast2);
        store_i64(lastDst + 24, xlast3);
    }

}

// A version of naiveSseMemcpyUnrolledAlignedV3, but with 64-byte iterations.
void naiveMemcpyUnrolledAlignedV3Cpp(char* dst, const char* src, size_t size)
{
    if (size <= 64) {
        // Copy [33-64], [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single load/stores with branches.
        if (size > 16) {
            if (size > 32) {
                // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
                int64_t x0 = load_i64(src);
                int64_t x1 = load_i64(src + 8);
                int64_t x2 = load_i64(src + 16);
                int64_t x3 = load_i64(src + 24);
                int64_t x4 = load_i64(src + size - 32);
                int64_t x5 = load_i64(src + size - 24);
                int64_t x6 = load_i64(src + size - 16);
                int64_t x7 = load_i64(src + size - 8);
                store_i64(dst, x0);
                store_i64(dst + 8, x1);
                store_i64(dst + 16, x2);
                store_i64(dst + 24, x3);
                store_i64(dst + size - 32, x4);
                store_i64(dst + size - 24, x5);
                store_i64(dst + size - 16, x6);
                store_i64(dst + size - 8, x7);
            } else {
                // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
                int64_t x0 = load_i64(src);
                int64_t x1 = load_i64(src + 8);
                int64_t x2 = load_i64(src + size - 16);
                int64_t x3 = load_i64(src + size - 8);
                store_i64(dst, x0);
                store_i64(dst + 8, x1);
                store_i64(dst + size - 16, x2);
                store_i64(dst + size - 8, x3);
            }
        } else {
            if (size > 8) {
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
        }
    } else {
        // Align the dst on 8-byte.
        size_t toAlignedDst = toAlignPtr<8>(dst);
        if (toAlignedDst != 0) {
            // We have at least 32 bytes, copy the first 8 one (including toAlignedDst bytes).
            int64_t x0 = load_i64(src);
            src += toAlignedDst;
            store_i64(dst, x0);
            dst += toAlignedDst;
            size -= toAlignedDst;
        }

        // Load the last 64 bytes. Useful if size % 64 != 0.
        int64_t xlast0 = load_i64(src + size - 64);
        int64_t xlast1 = load_i64(src + size - 56);
        int64_t xlast2 = load_i64(src + size - 48);
        int64_t xlast3 = load_i64(src + size - 40);
        int64_t xlast4 = load_i64(src + size - 32);
        int64_t xlast5 = load_i64(src + size - 24);
        int64_t xlast6 = load_i64(src + size - 16);
        int64_t xlast7 = load_i64(src + size - 8);
        char* lastDst = dst + size - 64;

        // Main loop: do 64-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
        // bytes).
        for (size_t i = (size - 1) / 64; i != 0; i--) {
            int64_t x0 = load_i64(src);
            int64_t x1 = load_i64(src + 8);
            int64_t x2 = load_i64(src + 16);
            int64_t x3 = load_i64(src + 24);
            int64_t x4 = load_i64(src + 32);
            int64_t x5 = load_i64(src + 40);
            int64_t x6 = load_i64(src + 48);
            int64_t x7 = load_i64(src + 56);
            src += 64;
            store_i64(dst, x0);
            store_i64(dst + 8, x1);
            store_i64(dst + 16, x2);
            store_i64(dst + 24, x3);
            store_i64(dst + 32, x4);
            store_i64(dst + 40, x5);
            store_i64(dst + 48, x6);
            store_i64(dst + 56, x7);
            dst += 64;
        }

        // Store the last 64 bytes (potentially overlapping with the last iter).
        store_i64(lastDst, xlast0);
        store_i64(lastDst + 8, xlast1);
        store_i64(lastDst + 16, xlast2);
        store_i64(lastDst + 24, xlast3);
        store_i64(lastDst + 32, xlast4);
        store_i64(lastDst + 40, xlast5);
        store_i64(lastDst + 48, xlast6);
        store_i64(lastDst + 56, xlast7);
    }

}

#if defined(CPU_IS_X86_64)

FORCE_INLINE __m128 load_v128(const char* p)
{
    return _mm_loadu_ps((const float*)p);
}

FORCE_INLINE void store_v128(char* p, __m128 v)
{
    return _mm_storeu_ps((float*)p, v);
}

FORCE_INLINE void storea_v128(char* p, __m128 v)
{
    return _mm_store_ps((float*)p, v);
}

#elif defined(CPU_IS_AARCH64)

FORCE_INLINE int8x16_t load_v128(const char* p)
{
    return vld1q_s8((const int8_t*)p);
}

FORCE_INLINE void store_v128(char* p, int8x16_t v)
{
    return vst1q_s8((int8_t*)p, v);
}

FORCE_INLINE void storea_v128(char* p, int8x16_t v)
{
    return vst1q_s8((int8_t*)p, v);
}

#else

#error "Arch unsupported"

#endif

#if defined(CPU_IS_X86_64) || defined(CPU_IS_AARCH64)

// A C++ reimplementation of naiveSseMemcpyUnrolledAlignedV2, templatized to provide different 128-bit types.
template<typename I128>
void naiveSimdMemcpyUnrolledAlignedV2Cpp(char* dst, const char* src, size_t size)
{
    IACA_START
    if (size <= 32) {
        // Copy [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single load/stores with branches.
        if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            I128 x0 = load_v128(src);
            I128 x1 = load_v128(src + size - 16);
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
            I128 x0 = load_v128(src);
            src += toAlignedDst;
            store_v128(dst, x0);
            dst += toAlignedDst;
            size -= toAlignedDst;
        }

        // Load the last 32 bytes. Useful if size % 32 != 0.
        I128 xlast0 = load_v128(src + size - 32);
        I128 xlast1 = load_v128(src + size - 16);
        char* lastDst = dst + size - 32;

        // Main loop: do 32-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
        // bytes).
        for (size_t i = (size - 1) / 32; i != 0; i--) {
            I128 x0 = load_v128(src);
            I128 x1 = load_v128(src + 16);
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

#endif

#if defined(CPU_IS_X86_64)

FORCE_INLINE __m256 load_v256(const char* p)
{
    return _mm256_loadu_ps((const float*)p);
}

FORCE_INLINE void store_v256(char* p, __m256 v)
{
    return _mm256_storeu_ps((float*)p, v);
}

FORCE_INLINE void storea_v256(char* p, __m256 v)
{
    return _mm256_store_ps((float*)p, v);
}

void naiveSseMemcpyUnrolledAlignedV2Cpp(char* dst, const char* src, size_t size)
{
    naiveSimdMemcpyUnrolledAlignedV2Cpp<__m128>(dst, src, size);
}

// A C++ reimplementation of naiveAvxMemcpyUnrolledAlignedV2.
void naiveAvxMemcpyUnrolledAlignedV2Cpp(char* dst, const char* src, size_t size)
{
    IACA_START
    if (size <= 64) {
        // Copy [33-64], [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single
        // load/stores with branches.
        if (size > 32) {
            __m256 y0 = load_v256(src);
            __m256 y1 = load_v256(src + size - 32);
            store_v256(dst, y0);
            store_v256(dst + size - 32, y1);
            _mm256_zeroupper();
        } else if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            __m128 x0 = load_v128(src);
            __m128 x1 = load_v128(src + size - 16);
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
            __m256 y0 = load_v256(src);
            src += toAlignedDst;
            store_v256(dst, y0);
            dst += toAlignedDst;
            size -= toAlignedDst;
        }

        // Load the last 64 bytes. Useful if size % 64 != 0.
        __m256 ylast0 = load_v256(src + size - 64);
        __m256 ylast1 = load_v256(src + size - 32);
        char* lastDst = dst + size - 64;

        // Main loop: do 64-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
        // bytes).
        for (size_t i = (size - 1) / 64; i != 0; i--) {
            __m256 y0 = load_v256(src);
            __m256 y1 = load_v256(src + 32);
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

// A version of naiveAvxMemcpyUnrolledAlignedV2 with even more size cases.
void naiveAvxMemcpyUnrolledAlignedV3Cpp(char* dst, const char* src, size_t size)
{
    IACA_START
    if (size <= 64) {
        // Copy [33-64], [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single
        // load/stores with branches.
        if (size > 32) {
            __m256 y0 = load_v256(src);
            __m256 y1 = load_v256(src + size - 32);
            store_v256(dst, y0);
            store_v256(dst + size - 32, y1);
            _mm256_zeroupper();
        } else if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            __m128 x0 = load_v128(src);
            __m128 x1 = load_v128(src + size - 16);
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
        __m256 ylast0 = load_v256(src + size - 64);
        __m256 ylast1 = load_v256(src + size - 32);
        char* lastDst = dst + size - 64;

        if (size <= 512) {
            if (size >= 256) {
                __m256 y0 = load_v256(src);
                __m256 y1 = load_v256(src + 32);
                __m256 y2 = load_v256(src + 64);
                __m256 y3 = load_v256(src + 96);
                __m256 y4 = load_v256(src + 128);
                __m256 y5 = load_v256(src + 160);
                __m256 y6 = load_v256(src + 192);
                __m256 y7 = load_v256(src + 224);
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
                __m256 y0 = load_v256(src);
                __m256 y1 = load_v256(src + 32);
                __m256 y2 = load_v256(src + 64);
                __m256 y3 = load_v256(src + 96);
                src += 128;
                store_v256(dst, y0);
                store_v256(dst + 32, y1);
                store_v256(dst + 64, y2);
                store_v256(dst + 96, y3);
                dst += 128;
                size -= 128;
            }
            if (size >= 64) {
                __m256 y0 = load_v256(src);
                __m256 y1 = load_v256(src + 32);
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
                __m256 y0 = load_v256(src);
                src += toAlignedDst;
                store_v256(dst, y0);
                dst += toAlignedDst;
                size -= toAlignedDst;
            }

            // Main loop: do 64-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
            // bytes).
            for (size_t i = (size - 1) / 64; i != 0; i--) {
                __m256 y0 = load_v256(src);
                __m256 y1 = load_v256(src + 32);
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

#elif defined(CPU_IS_AARCH64)

void naiveNeonMemcpyUnrolledAlignedV2Cpp(char* dst, const char* src, size_t size)
{
    naiveSimdMemcpyUnrolledAlignedV2Cpp<int8x16_t>(dst, src, size);
}

#endif // CPU_IS_X86_64 || CPU_IS_AARCH64
