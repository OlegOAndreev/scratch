#pragma once

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

// Define SIZE_T_BITS
#if SIZE_MAX == 0xFFFFFFFF
#define SIZE_T_BITS 32
#elif SIZE_MAX == 0xFFFFFFFFFFFFFFFF
#define SIZE_T_BITS 64
#else
#error "Unsupported SIZE_MAX"
#endif

// Define FORCE_INLINE
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

// Returns the exponent e such that 2^(e - 1) <= v < 2^e.
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
#endif
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
// Time-related functions.
//

// Returns current time counter in ticks, frequency specified by getTimeFreq.
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

// Returns timer frequency.
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

// Returns elapsed milliseconds since startTime ticks.
inline int elapsedMsec(uint64_t startTime)
{
    return (getTimeCounter() - startTime) * 1000LL / getTimeFreq();
}


//
// Random number generation.
//

// Copied from https://en.wikipedia.org/wiki/Xorshift
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

// Reduces x to range [0, N), an alternative to x % N.
// Taken from https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction/
inline uint32_t reduceRange(uint32_t x, uint32_t N)
{
    return ((uint64_t) x * (uint64_t) N) >> 32;
}

// Returns random values in range [from, to) with xorshift128 state.
inline uint32_t randomRange(uint32_t state[4], uint32_t from, uint32_t to)
{
    return from + reduceRange(xorshift128(state), to - from);
}


//
// Containers
//

// Returns statically determined size of an array.
template <typename T, size_t N>
size_t arraySize(const T(&)[N])
{
    return N;
}

// Computes a very simple hash, see: http://www.eecs.harvard.edu/margo/papers/usenix91/paper.ps
inline size_t simpleHash(char const* s, size_t size)
{
    size_t hash = 0;
    for (char const* it = s, * end = s + size; it != end; ++it)
        hash = *it + (hash << 6) + (hash << 16) - hash;
    return hash;
}
