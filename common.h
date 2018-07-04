#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__APPLE__)
#include <sys/time.h>
#elif defined(__linux__)
#include <time.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#error "Unsupported OS"
#endif

//
// Preprocessor
//

//
// Defines SIZE_T_BITS.
//
#if SIZE_MAX == 0xFFFFFFFF
#define SIZE_T_BITS 32
#elif SIZE_MAX == 0xFFFFFFFFFFFFFFFF
#define SIZE_T_BITS 64
#else
#error "Unsupported SIZE_MAX"
#endif

// Validates that the platform is "sane".
static_assert(sizeof(size_t) == sizeof(ptrdiff_t), "Very strange platform");
static_assert(sizeof(size_t) == sizeof(intptr_t), "Very strange platform");
static_assert(sizeof(size_t) == sizeof(uintptr_t), "Very strange platform");

//
// Defines FORCE_INLINE
//
#if defined(__clang__) || defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE
#endif

//
// Bit manipulation
//

//
// Returns the exponent e such that 2^(e - 1) <= v < 2^e.
//
inline int nextLog2(size_t v)
{
#if defined(__clang__) || defined(__GNUC__)
    if (v == 0) {
        return 0;
    } else {
#if SIZE_T_BITS == 32
        return sizeof(size_t) * 8 - __builtin_clz(v);
#elif SIZE_T_BITS == 64
        return sizeof(size_t) * 8 - __builtin_clzl(v);
#else
#error "Unsupported SIZE_T_BITS"
#endif
    }
#elif defined(_MSC_VER)
    unsigned char nonzero;
    unsigned long index;
#if SIZE_T_BITS == 32
    nonzero = _BitScanReverse(&index, v);
#elif SIZE_T_BITS == 64
    nonzero = _BitScanReverse64(&index, v);
#else
#error "Unsupported SIZE_T_BITS"
#endif
    if (nonzero) {
        return index + 1;
    } else {
        return 0;
    }
#else
    int e = 0;
    while (v != 0) {
        e++;
        v >>= 1;
    }
    return e;
#endif
}

//
// Loading and storing values without aliasing violations.
//
#if defined(__clang__) || defined(__GNUC__)
// Macros, useful for defining a pair of functions type load_postfix(const char* p) and void store_postfix(char* p, type v),
// e.g. load_i8/store_i8 for loading/storing int8_t.
#define DEFINE_LOAD_STORE(type, postfix) \
FORCE_INLINE type load_##postfix(void const* p) \
{ \
    type v; \
    __builtin_memcpy(&v, p, sizeof(v)); \
    return v; \
} \
FORCE_INLINE void store_##postfix(void* p, type v) \
{ \
    __builtin_memcpy(p, &v, sizeof(v)); \
}

#elif defined(_MSC_VER)
#define DEFINE_LOAD_STORE(type, postfix) \
FORCE_INLINE type load_##postfix(void const* p) \
{ \
    return *(const type*)p; \
} \
FORCE_INLINE void store_##postfix(void* p, type v) \
{ \
    *(type*)p = v; \
}

#else
#error "Unsupported compiler"
#endif

//
// Defines load_i8, load_u8, load_i16, load_u16, load_i32, load_u32, load_i64, load_u64
//
DEFINE_LOAD_STORE(int8_t, i8)
DEFINE_LOAD_STORE(uint8_t, u8)
DEFINE_LOAD_STORE(int16_t, i16)
DEFINE_LOAD_STORE(uint16_t, u16)
DEFINE_LOAD_STORE(int32_t, i32)
DEFINE_LOAD_STORE(uint32_t, u32)
DEFINE_LOAD_STORE(int64_t, i64)
DEFINE_LOAD_STORE(uint64_t, u64)

//
// Endianess.
//
#if defined(__clang__) || defined(__GNUC__)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define COMMON_LITTLE_ENDIAN
#else
#define COMMON_BIG_ENDIAN
#endif
#else
#define COMMON_LITTLE_ENDIAN
#endif

//
// Byte swapping
//
#if defined(__clang__) || defined(__GNUC__)
uint16_t byteSwap(uint16_t v)
{
    return __builtin_bswap16(v);
}
uint32_t byteSwap(uint32_t v)
{
    return __builtin_bswap32(v);
}
uint64_t byteSwap(uint64_t v)
{
    return __builtin_bswap64(v);
}
#elif defined(_MSC_VER)
uint16_t byteSwap(uint16_t v)
{
    static_assert(sizeof(unsigned short) == 2, "Sanity check failed");
    return _byteswap_ushort(v);
}
uint32_t byteSwap(uint32_t v)
{
    static_assert(sizeof(unsigned long) == 4, "Sanity check failed");
    return _byteswap_ulong(v);
}
uint64_t byteSwap(uint64_t v)
{
    static_assert(sizeof(unsigned __int64) == 8, "Sanity check failed");
    return _byteswap_uint64(v);
}
#else
// We could provide the default implementations here, but it takes too much space.
#error "Unsupported compiler"
#endif

// Signed versions of byteSwap(), the unsigned -> signed conversions are implementation-defined.
int16_t byteSwap(int16_t v)
{
    return (int16_t)byteSwap((uint16_t)v);
}
int32_t byteSwap(int32_t v)
{
    return (int32_t)byteSwap((uint32_t)v);
}
int64_t byteSwap(int64_t v)
{
    return (int64_t)byteSwap((uint64_t)v);
}
#if SIZE_T_BITS == 32
size_t byteSwap(size_t v)
{
    return (size_t)byteSwap((uint32_t)v);
}
#elif SIZE_T_BITS == 64
size_t byteSwap(size_t v)
{
    return (size_t)byteSwap((uint64_t)v);
}
#else
#error "Unsupported SIZE_T_BITS"
#endif
#if SIZE_T_BITS == 32
size_t byteSwap(ptrdiff_t v)
{
    return (ptrdiff_t)byteSwap((int32_t)v);
}
#elif SIZE_T_BITS == 64
ptrdiff_t byteSwap(ptrdiff_t v)
{
    return (ptrdiff_t)byteSwap((int64_t)v);
}
#else
#error "Unsupported SIZE_T_BITS"
#endif

//
// Time-related functions.
//

//
// Returns current time counter in ticks, frequency specified by getTimeFreq.
//
inline int64_t getTimeCounter()
{
#if defined(__APPLE__)
    timeval tp;
    gettimeofday(&tp, nullptr);
    return tp.tv_sec * 1000000ULL + tp.tv_usec;
#elif defined(__linux__)
    timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
#elif defined(_WIN32)
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
#else
#error "Unsupported OS"
#endif
}

//
// Returns timer frequency.
//
inline int64_t getTimeFreq()
{
#if defined(__APPLE__)
    return 1000000;
#elif defined(__linux__)
    return 1000000000;
#elif defined(_WIN32)
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    return li.QuadPart;
#else
#error "Unsupported OS"
#endif
}

//
// Returns elapsed milliseconds since startTime ticks.
//
inline int elapsedMsec(uint64_t startTime)
{
    return (getTimeCounter() - startTime) * 1000LL / getTimeFreq();
}


//
// Random number generation.
//

//
// Copied from https://en.wikipedia.org/wiki/Xorshift
//
inline uint32_t xorshift128(uint32_t state[4])
{
    /* Algorithm "xor128" from p. 5 of Marsaglia, "Xorshift RNGs" */
    uint32_t s, t = state[3];
    t ^= t << 11;
    t ^= t >> 8;
    state[3] = state[2]; state[2] = state[1]; state[1] = s = state[0];
    t ^= s;
    t ^= s >> 19;
    state[0] = t;
    return t;
}

//
// Reduces x to range [0, N), an alternative to x % N.
// Taken from https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction/
//
inline uint32_t reduceRange(uint32_t x, uint32_t N)
{
    return ((uint64_t) x * (uint64_t) N) >> 32;
}

//
// Returns random values in range [from, to) with xorshift128 state.
//
inline uint32_t randomRange(uint32_t state[4], uint32_t from, uint32_t to)
{
    return from + reduceRange(xorshift128(state), to - from);
}


//
// Containers
//

//
// Returns statically determined size of an array.
//
template <typename T, size_t N>
size_t arraySize(const T(&)[N])
{
    return N;
}

//
// Computes a very simple hash, see: http://www.eecs.harvard.edu/margo/papers/usenix91/paper.ps
//
inline size_t simpleHash(char const* s, size_t size)
{
    size_t hash = 0;
    for (char const* it = s, * end = s + size; it != end; ++it)
        hash = *it + (hash << 6) + (hash << 16) - hash;
    return hash;
}
