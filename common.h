#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <utility>

#if defined(__APPLE__)
#include <sys/time.h>
#elif defined(__linux__)
#include <time.h>
#include <sys/prctl.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#error "Unsupported OS"
#endif

//
// Preprocessor
//

//
// Defines COMMON_INT_BITS, COMMON_LONG_BITS, COMMON_LONG_LONG_BITS and COMMON_SIZE_T_BITS.
//
#if UINT_MAX == 0xFFFFFFFF
#define COMMON_INT_BITS 32
#elif UINT_MAX == 0xFFFFFFFFFFFFFFFF
#define COMMON_INT_BITS 64
#else
#error "Unsupported UINT_MAX"
#endif
#if ULONG_MAX == 0xFFFFFFFF
#define COMMON_LONG_BITS 32
#elif ULONG_MAX == 0xFFFFFFFFFFFFFFFF
#define COMMON_LONG_BITS 64
#else
#error "Unsupported ULONG_MAX"
#endif
#if ULLONG_MAX == 0xFFFFFFFF
#define COMMON_LONG_LONG_BITS 32
#elif ULLONG_MAX == 0xFFFFFFFFFFFFFFFF
#define COMMON_LONG_LONG_BITS 64
#else
#error "Unsupported ULLONG_MAX"
#endif
#if SIZE_MAX == 0xFFFFFFFF
#define COMMON_SIZE_T_BITS 32
#elif SIZE_MAX == 0xFFFFFFFFFFFFFFFF
#define COMMON_SIZE_T_BITS 64
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
#define NO_INLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#define NO_INLINE __declspec(noinline)
#else
#define FORCE_INLINE
#define NO_INLINE
#endif

//
// Error reporting
//

#if !defined(ENSURE)

#include <cstdio>
#include <cstdlib>

#define ENSURE(cond, message) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s:%d: %s: %s\n", __FILE__, __LINE__, #cond, message); \
        abort(); \
    } \
} while (0)

#endif

#if !defined(FAIL)

#include <cstdio>
#include <cstdlib>

#define FAIL(message) do { \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, message); \
    abort(); \
} while (0)

#endif

//
// Bit manipulation
//

// Returns the exponent e such that 2^(e - 1) <= v < 2^e.
FORCE_INLINE int nextLog2(size_t v)
{
#if defined(__clang__) || defined(__GNUC__)
    if (v == 0) {
        return 0;
    } else {
#if COMMON_SIZE_T_BITS == 32
        return sizeof(size_t) * 8 - __builtin_clz(v);
#elif COMMON_SIZE_T_BITS == 64
        return sizeof(size_t) * 8 - __builtin_clzl(v);
#else
#error "Unsupported COMMON_SIZE_T_BITS"
#endif
    }
#elif defined(_MSC_VER)
    unsigned char nonzero;
    unsigned long index;
#if COMMON_SIZE_T_BITS == 32
    nonzero = _BitScanReverse(&index, v);
#elif COMMON_SIZE_T_BITS == 64
    nonzero = _BitScanReverse64(&index, v);
#else
#error "Unsupported COMMON_SIZE_T_BITS"
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

// Defines load_i8, load_u8, load_i16, load_u16, load_i32, load_u32, load_i64, load_u64
DEFINE_LOAD_STORE(int8_t, i8)
DEFINE_LOAD_STORE(uint8_t, u8)
DEFINE_LOAD_STORE(int16_t, i16)
DEFINE_LOAD_STORE(uint16_t, u16)
DEFINE_LOAD_STORE(int32_t, i32)
DEFINE_LOAD_STORE(uint32_t, u32)
DEFINE_LOAD_STORE(int64_t, i64)
DEFINE_LOAD_STORE(uint64_t, u64)
DEFINE_LOAD_STORE(void*, ptr)

#if defined(__x86_64__) || defined(_M_X64)
#define CPU_IS_X86_64
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CPU_IS_AARCH64
#endif

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
FORCE_INLINE unsigned short byteSwap(unsigned short v)
{
    static_assert(sizeof(unsigned short) == 2, "Unsupported short size");
    return __builtin_bswap16(v);
}
FORCE_INLINE unsigned int byteSwap(unsigned int v)
{
#if COMMON_INT_BITS == 32
    return __builtin_bswap32(v);
#elif COMMON_INT_BITS == 64
    return __builtin_bswap64(v);
#else
#error "Unsupported COMMON_INT_BITS value"
#endif
}
FORCE_INLINE unsigned long byteSwap(unsigned long v)
{
#if COMMON_LONG_BITS == 32
    return __builtin_bswap32(v);
#elif COMMON_LONG_BITS == 64
    return __builtin_bswap64(v);
#else
#error "Unsupported COMMON_LONG_BITS value"
#endif
}
FORCE_INLINE unsigned long long byteSwap(unsigned long long v)
{
#if COMMON_LONG_LONG_BITS == 32
    return __builtin_bswap32(v);
#elif COMMON_LONG_LONG_BITS == 64
    return __builtin_bswap64(v);
#else
#error "Unsupported COMMON_LONG_LONG_BITS value"
#endif
}
#elif defined(_MSC_VER)
FORCE_INLINE unsigned int byteSwap(unsigned int v)
{
    return _byteswap_ushort(v);
}
FORCE_INLINE unsigned __int32 byteSwap(unsigned int v)
{
    return _byteswap_ulong(v);
}
FORCE_INLINE unsigned long byteSwap(unsigned long v)
{
    return _byteswap_ulong(v);
}
FORCE_INLINE unsigned __int64 byteSwap(unsigned __int64 v)
{
    return _byteswap_uint64(v);
}
#else
// We could provide the default implementations here, but it takes too much space.
#error "Unsupported compiler"
#endif

// Signed versions of byteSwap(), the unsigned -> signed conversions are implementation-defined.
FORCE_INLINE short byteSwap(short v)
{
    return (short)byteSwap((unsigned short)v);
}
FORCE_INLINE int byteSwap(int v)
{
    return (int)byteSwap((unsigned int)v);
}
FORCE_INLINE long byteSwap(long v)
{
    return (long)byteSwap((unsigned long)v);
}
FORCE_INLINE long long byteSwap(long long v)
{
    return (long long)byteSwap((unsigned long long)v);
}


//
// Pointer alignment.
//

// Returns the first pointer after ptr, which is aligned according to alignment.
template<size_t alignment, typename T>
FORCE_INLINE T* nextAlignedPtr(T* ptr)
{
    size_t remainder = alignment - 1 - ((uintptr_t)ptr + alignment - 1) % alignment;
    return (T*)((uintptr_t)ptr + remainder);
}


//
// Cache-line size (very crude!)
//

#define CACHE_LINE_SIZE 64


//
// Time-related functions.
//

// Returns current time counter in ticks, frequency specified by getTimeFreq.
inline int64_t getTimeTicks()
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
    return (getTimeTicks() - startTime) * 1000LL / getTimeFreq();
}

// Sleep for the given number of milliseconds. See enableFinegrainedSleep() for precision details.
inline void sleepMsec(int msec)
{
#if defined(__APPLE__) || defined(__linux__)
    struct timespec ts;
    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec - ts.tv_sec * 1000) * 1000000;
    nanosleep(&ts, nullptr);
#elif defined(_WIN32)
    Sleep(msec);
#else
#error "Unsupported OS"
#endif
}

// Macos has ~10usec sleep granularity. Linux has ~50-100usec sleep granularity by default (can be
// configured via prctl(PR_SET_TIMERSLACK) down to ~10usec). Windows by default has a very coarse
// granularity of 15msec. The granularity for the current process on Win32 can be lowered down
// to 1msec by calling this function once before calling the sleepMsec().
//
// NOTE: Finer granularities (~100nsec) can be achieved by sleeping + spinning for
// the last microseconds.
inline void enableFinegrainedSleep()
{
#if defined(__linux__)
    prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
#elif defined(_WIN32)
    timeBeginPeriod(1);
#endif
}

//
// Semaphore: an OS semaphore class with two methods: post() and wait().
//

#if defined(__linux__)
#include <semaphore.h>

struct Semaphore {
    sem_t sema;
    Semaphore() { sem_init(&sema, 0, 0); }
    Semaphore(unsigned value) { sem_init(&sema, 0, value); }
    ~Semaphore() { sem_destroy(&sema); }
    void post() { sem_post(&sema); }
    void wait() { sem_wait(&sema); }
};

#elif defined(__APPLE__)
#include <dispatch/dispatch.h>

struct Semaphore {
    dispatch_semaphore_t sema;
    Semaphore() { sema = dispatch_semaphore_create(0); }
    Semaphore(unsigned value) { sema = dispatch_semaphore_create(value); }
    ~Semaphore() { dispatch_release(sema); }
    void post() { dispatch_semaphore_signal(sema); }
    void wait() { dispatch_semaphore_wait(sema, ~uint64_t(0)); }
};

#elif defined(_WIN32)
#include <windows.h>

struct Semaphore {
    HANDLE sema;
    Semaphore() { sema = CreateSemaphore(NULL, 0, MAXLONG, NULL); }
    Semaphore(unsigned value) { sema = CreateSemaphore(NULL, value, MAXLONG, NULL); }
    ~Semaphore() { CloseHandle(sema); }
    void post() { ReleaseSemaphore(sema, 1, NULL); }
    void wait() { WaitForSingleObject(sema, INFINITE); }
};

#else
#error "Unsupported OS"
#endif


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

// An alternative to std::remove_cvref from C++20 (does not support volatile, though).
template<typename T>
struct RemoveCRef {
    using Type = T;
};

template<typename T>
struct RemoveCRef<T const> {
    using Type = T;
};

template<typename T>
struct RemoveCRef<T&> {
    using Type = T;
};

template<typename T>
struct RemoveCRef<T const&> {
    using Type = T;
};

template<typename T>
struct RemoveCRef<T&&> {
    using Type = T;
};

template<typename T>
struct RemoveCRef<T const&&> {
    using Type = T;
};

// Computes a very simple hash, see: http://www.eecs.harvard.edu/margo/papers/usenix91/paper.ps
inline size_t simpleHash(char const* s, size_t size)
{
    size_t hash = 0;
    for (char const* it = s, * end = s + size; it != end; ++it)
        hash = *it + (hash << 6) + (hash << 16) - hash;
    return hash;
}

// Returns the average of the elements. NOTE: ASSUMES THAT YOU CAN CALCULATE SUM OF ALL
// VALUES WITHOUT OVERFLOW. Should work almost the same
// as std::accumulate(begin, end, {}) / (end - begin).
template<typename It>
inline auto simpleAverage(It begin, It end) -> typename RemoveCRef<decltype(*begin)>::Type
{
    typename RemoveCRef<decltype(*begin)>::Type sum{};
    if (begin == end) {
        return sum;
    } else {
        for (It it = begin; it!= end; ++it) {
            sum += *it;
        }
        return sum / (end - begin);
    }
}

// Returns average of the container elements. See simpleAverage(It begin, It end)
// for NOTE on the assumptions.
template<typename C>
inline auto simpleAverage(C const& container)
    -> typename RemoveCRef<decltype(*container.begin())>::Type
{
    return simpleAverage(container.begin(), container.end());
}

// Returns true if set-like container contains value, false if not.
template<typename S, typename V>
inline bool setContains(S const& setContainer, V const& value)
{
    return setContainer.find(value) != setContainer.end();
}

// Removes the elements satisfying the predicate from the vector-like container (container
// must have methods begin(), end() and erase(fromIt, toIt)). Preserves the order of the elements
// in the original container.
template<typename V, typename P>
inline void removeIf(V& vecContainer, P const& predicate)
{
    auto it = vecContainer.begin();
    auto endIt = vecContainer.end();
    // Optimization: do not call std::move until we actually encounter an element to remove.
    while (it != endIt && !predicate(*it)) {
        ++it;
    }
    if (it == endIt) {
        return;
    }
    // We know that predicate(*it) == true.
    auto insertIt = it;
    ++it;
    for (; it != endIt; ++it) {
        if (predicate(*it)) {
            continue;
        }
        *insertIt = std::move(*it);
        ++insertIt;
    }
    vecContainer.erase(insertIt, endIt);
}
