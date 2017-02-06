#include <immintrin.h>
#include <stdint.h>

#if defined(WITH_IACA_SUPPORT)
#include <iacaMarks.h>
#else
#define IACA_START
#define IACA_END
#endif

#if defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline)) inline

// Defines a pair of functions type load_postfix(const char* p) and void store_postfix(char* p, type v),
// e.g. load_i8/store_i8 for loading/storing int8_t.
#define DEFINE_LOAD_STORE(type, postfix) \
FORCE_INLINE type load_##postfix(const char* p) \
{ \
    type v; \
    __builtin_memcpy(&v, p, sizeof(v)); \
    return v; \
} \
FORCE_INLINE void store_##postfix(char* p, type v) \
{ \
    __builtin_memcpy(p, &v, sizeof(v)); \
}

#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline

#define DEFINE_LOAD_STORE(type, postfix) \
FORCE_INLINE type load_##postfix(const char* p) \
{ \
    return *(const type*)p; \
} \
FORCE_INLINE void store_##postfix(const char* p, type v) \
{ \
    *(type*)p = v; \
}

#else
#error "Unsupported compiler"
#endif

DEFINE_LOAD_STORE(int8_t, i8)
DEFINE_LOAD_STORE(int16_t, i16)
DEFINE_LOAD_STORE(int32_t, i32)
DEFINE_LOAD_STORE(int64_t, i64)

FORCE_INLINE __m128 load_f128(const char* p)
{
    return _mm_loadu_ps((const float*)p);
}

FORCE_INLINE void store_f128(char* p, __m128 v)
{
    return _mm_storeu_ps((float*)p, v);
}

FORCE_INLINE void storea_f128(char* p, __m128 v)
{
    return _mm_store_ps((float*)p, v);
}

FORCE_INLINE __m256 load_f256(const char* p)
{
    return _mm256_loadu_ps((const float*)p);
}

FORCE_INLINE void store_f256(char* p, __m256 v)
{
    return _mm256_storeu_ps((float*)p, v);
}

FORCE_INLINE void storea_f256(char* p, __m256 v)
{
    return _mm256_store_ps((float*)p, v);
}

// Returns the distance to the next aligned pointer after p.
template <size_t alignment>
FORCE_INLINE size_t toAlignPtr(char* p)
{
    return (UINTPTR_MAX - (uintptr_t)p + 1) & (alignment - 1);
}

// A C++ reimplementation of naiveSseMemcpyUnrolledV2.
void naiveSseMemcpyUnrolledV2Cpp(char* dst, const char* src, size_t size)
{
    IACA_START
    if (size <= 32) {
        // Copy [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single load/stores with branches.
        if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            __m128 x0 = load_f128(src);
            __m128 x1 = load_f128(src + size - 16);
            store_f128(dst, x0);
            store_f128(dst + size - 16, x1);
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
            __m128 x0 = load_f128(src);
            src += toAlignedDst;
            store_f128(dst, x0);
            dst += toAlignedDst;
            size -= toAlignedDst;
        }

        // Load the last 32 bytes. Useful if size % 32 != 0.
        __m128 xlast0 = load_f128(src + size - 32);
        __m128 xlast1 = load_f128(src + size - 16);
        char* lastDst = dst + size - 32;

        // Main loop: do 32-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
        // bytes).
        for (size_t i = (size - 1) / 32; i != 0; i--) {
            __m128 x0 = load_f128(src);
            __m128 x1 = load_f128(src + 16);
            src += 32;
            storea_f128(dst, x0);
            storea_f128(dst + 16, x1);
            dst += 32;
        }

        // Store the last 32 bytes (potentially overlapping with the last iter).
        store_f128(lastDst, xlast0);
        store_f128(lastDst + 16, xlast1);
    }
    IACA_END
}

// A C++ reimplementation of naiveAvxMemcpyUnrolledV2.
void naiveAvxMemcpyUnrolledV2Cpp(char* dst, const char* src, size_t size)
{
    IACA_START
    if (size <= 64) {
        // Copy [33-64], [17-32], [9-16], [5-8] via two load/stores, the 1-4 bytes via single
        // load/stores with branches.
        if (size > 32) {
            __m256 y0 = load_f256(src);
            __m256 y1 = load_f256(src + size - 32);
            store_f256(dst, y0);
            store_f256(dst + size - 32, y1);
            _mm256_zeroupper();
        } else if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            __m128 x0 = load_f128(src);
            __m128 x1 = load_f128(src + size - 16);
            store_f128(dst, x0);
            store_f128(dst + size - 16, x1);
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
            __m256 y0 = load_f256(src);
            src += toAlignedDst;
            store_f256(dst, y0);
            dst += toAlignedDst;
            size -= toAlignedDst;
        }

        // Load the last 64 bytes. Useful if size % 64 != 0.
        __m256 ylast0 = load_f256(src + size - 64);
        __m256 ylast1 = load_f256(src + size - 32);
        char* lastDst = dst + size - 64;

        // Main loop: do 64-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
        // bytes).
        for (size_t i = (size - 1) / 64; i != 0; i--) {
            __m256 y0 = load_f256(src);
            __m256 y1 = load_f256(src + 32);
            src += 64;
            storea_f256(dst, y0);
            storea_f256(dst + 32, y1);
            dst += 64;
        }

        // Store the last 64 bytes (potentially overlapping with the last iter).
        store_f256(lastDst, ylast0);
        store_f256(lastDst + 32, ylast1);
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
            __m256 y0 = load_f256(src);
            __m256 y1 = load_f256(src + size - 32);
            store_f256(dst, y0);
            store_f256(dst + size - 32, y1);
            _mm256_zeroupper();
        } else if (size > 16) {
            // Copy the first 16 bytes and the last 16 bytes (potentially overlapping).
            __m128 x0 = load_f128(src);
            __m128 x1 = load_f128(src + size - 16);
            store_f128(dst, x0);
            store_f128(dst + size - 16, x1);
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
        __m256 ylast0 = load_f256(src + size - 64);
        __m256 ylast1 = load_f256(src + size - 32);
        char* lastDst = dst + size - 64;

        if (size <= 512) {
            if (size >= 256) {
                __m256 y0 = load_f256(src);
                __m256 y1 = load_f256(src + 32);
                __m256 y2 = load_f256(src + 64);
                __m256 y3 = load_f256(src + 96);
                __m256 y4 = load_f256(src + 128);
                __m256 y5 = load_f256(src + 160);
                __m256 y6 = load_f256(src + 192);
                __m256 y7 = load_f256(src + 224);
                src += 256;
                store_f256(dst, y0);
                store_f256(dst + 32, y1);
                store_f256(dst + 64, y2);
                store_f256(dst + 96, y3);
                store_f256(dst + 128, y4);
                store_f256(dst + 160, y5);
                store_f256(dst + 192, y6);
                store_f256(dst + 224, y7);
                dst += 256;
                size -= 256;
            }
            if (size >= 128) {
                __m256 y0 = load_f256(src);
                __m256 y1 = load_f256(src + 32);
                __m256 y2 = load_f256(src + 64);
                __m256 y3 = load_f256(src + 96);
                src += 128;
                store_f256(dst, y0);
                store_f256(dst + 32, y1);
                store_f256(dst + 64, y2);
                store_f256(dst + 96, y3);
                dst += 128;
                size -= 128;
            }
            if (size >= 64) {
                __m256 y0 = load_f256(src);
                __m256 y1 = load_f256(src + 32);
                src += 64;
                store_f256(dst, y0);
                store_f256(dst + 32, y1);
                dst += 64;
                size -= 64;
            }
        } else {
            // Align the dst on 32-byte
            size_t toAlignedDst = toAlignPtr<32>(dst);
            if (toAlignedDst != 0) {
                // We have at least 64 bytes, copy the first 32 one (including toAlignedDst bytes).
                __m256 y0 = load_f256(src);
                src += toAlignedDst;
                store_f256(dst, y0);
                dst += toAlignedDst;
                size -= toAlignedDst;
            }

            // Main loop: do 64-byte iters (one less iter if size % 64 == 0, because we have already loaded the last 64
            // bytes).
            for (size_t i = (size - 1) / 64; i != 0; i--) {
                __m256 y0 = load_f256(src);
                __m256 y1 = load_f256(src + 32);
                src += 64;
                storea_f256(dst, y0);
                storea_f256(dst + 32, y1);
                dst += 64;
            }
        }

        // Store the last 64 bytes (potentially overlapping with the last iter).
        store_f256(lastDst, ylast0);
        store_f256(lastDst + 32, ylast1);
        _mm256_zeroupper();
    }
    IACA_END
}
