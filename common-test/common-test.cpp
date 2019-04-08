#include "common.h"

#include <thread>
#include <type_traits>
#include <unordered_set>
#include <vector>


// Tests that FORCE_INLINE compiles.
FORCE_INLINE void testForceInline()
{
}

// Tests that NO_INLINE compiles.
NO_INLINE void testNoInline()
{
}

void testNextLog2()
{
    ENSURE(nextLog2(0) == 0, "");
    ENSURE(nextLog2(1) == 1, "");
    ENSURE(nextLog2(2) == 2, "");
    ENSURE(nextLog2(3) == 2, "");
    ENSURE(nextLog2(4) == 3, "");
    ENSURE(nextLog2(5) == 3, "");
    ENSURE(nextLog2(6) == 3, "");
    ENSURE(nextLog2(7) == 3, "");
    ENSURE(nextLog2(8) == 4, "");
    ENSURE(nextLog2(9) == 4, "");
    for (size_t i = 0; i < COMMON_SIZE_T_BITS; i++) {
        ENSURE((size_t)nextLog2((size_t)1 << i) == i + 1, "");
        ENSURE((size_t)nextLog2(((size_t)1 << i) - 1) == i, "");
    }
    ENSURE(nextLog2(SIZE_MAX) == COMMON_SIZE_T_BITS, "");
    printf("testNextLog2 passed\n");
}

uint16_t makeU16(uint8_t a, uint8_t b)
{
    return ((uint16_t)a << 8) + b;
}

int16_t makeI16(uint8_t a, uint8_t b)
{
    return (int16_t)makeU16(a, b);
}

uint32_t makeU32(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return ((uint32_t)makeU16(a, b) << 16) + makeU16(c, d);
}

int32_t makeI32(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (int32_t)makeU32(a, b, c, d);
}

uint64_t makeU64(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                 uint8_t e, uint8_t f, uint8_t g, uint8_t h)
{
    return ((uint64_t)makeU32(a, b, c, d) << 32) + makeU32(e, f, g, h);
}

int64_t makeI64(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                uint8_t e, uint8_t f, uint8_t g, uint8_t h)
{
    return (int64_t)makeU64(a, b, c, d, e, f, g, h);
}

void testLoadStores()
{
    uint8_t buffer[9] = {0, 1, 2, 3, 251, 252, 253, 254, 255};

    ENSURE(load_i8(buffer) == 0, "");
    ENSURE(load_i8(buffer + 1) == 1, "");
    ENSURE(load_i8(buffer + 2) == 2, "");
    ENSURE(load_i8(buffer + 3) == 3, "");
    ENSURE(load_i8(buffer + 4) == -5, "");
    ENSURE(load_i8(buffer + 5) == -4, "");
    ENSURE(load_i8(buffer + 6) == -3, "");
    ENSURE(load_i8(buffer + 7) == -2, "");
    ENSURE(load_i8(buffer + 8) == -1, "");

    ENSURE(load_u8(buffer) == 0, "");
    ENSURE(load_u8(buffer + 1) == 1, "");
    ENSURE(load_u8(buffer + 2) == 2, "");
    ENSURE(load_u8(buffer + 3) == 3, "");
    ENSURE(load_u8(buffer + 4) == 251, "");
    ENSURE(load_u8(buffer + 5) == 252, "");
    ENSURE(load_u8(buffer + 6) == 253, "");
    ENSURE(load_u8(buffer + 7) == 254, "");
    ENSURE(load_u8(buffer + 8) == 255, "");

#if defined(COMMON_LITTLE_ENDIAN)
    ENSURE(load_i16(buffer) == makeI16(1, 0), "");
    ENSURE(load_i16(buffer + 1) == makeI16(2, 1), "");
    ENSURE(load_i16(buffer + 2) == makeI16(3, 2), "");
    ENSURE(load_i16(buffer + 3) == makeI16(251, 3), "");
    ENSURE(load_i16(buffer + 4) == makeI16(252, 251), "");
    ENSURE(load_i16(buffer + 5) == makeI16(253, 252), "");
    ENSURE(load_i16(buffer + 6) == makeI16(254, 253), "");
    ENSURE(load_i16(buffer + 7) == makeI16(255, 254), "");

    ENSURE(load_u16(buffer) == makeU16(1, 0), "");
    ENSURE(load_u16(buffer + 1) == makeU16(2, 1), "");
    ENSURE(load_u16(buffer + 2) == makeU16(3, 2), "");
    ENSURE(load_u16(buffer + 3) == makeU16(251, 3), "");
    ENSURE(load_u16(buffer + 4) == makeU16(252, 251), "");
    ENSURE(load_u16(buffer + 5) == makeU16(253, 252), "");
    ENSURE(load_u16(buffer + 6) == makeU16(254, 253), "");
    ENSURE(load_u16(buffer + 7) == makeU16(255, 254), "");
#else
    ENSURE(load_i16(buffer) == makeI16(0, 1), "");
    ENSURE(load_i16(buffer + 1) == makeI16(1, 2), "");
    ENSURE(load_i16(buffer + 2) == makeI16(2, 3), "");
    ENSURE(load_i16(buffer + 3) == makeI16(3, 251), "");
    ENSURE(load_i16(buffer + 4) == makeI16(251, 252), "");
    ENSURE(load_i16(buffer + 5) == makeI16(252, 253), "");
    ENSURE(load_i16(buffer + 6) == makeI16(253, 254), "");
    ENSURE(load_i16(buffer + 7) == makeI16(254, 255), "");

    ENSURE(load_u16(buffer) == makeU16(0, 1), "");
    ENSURE(load_u16(buffer + 1) == makeU16(1, 2), "");
    ENSURE(load_u16(buffer + 2) == makeU16(2, 3), "");
    ENSURE(load_u16(buffer + 3) == makeU16(3, 251), "");
    ENSURE(load_u16(buffer + 4) == makeU16(251, 252), "");
    ENSURE(load_u16(buffer + 5) == makeU16(252, 253), "");
    ENSURE(load_u16(buffer + 6) == makeU16(253, 254), "");
    ENSURE(load_u16(buffer + 7) == makeU16(254, 255), "");
#endif

#if defined(COMMON_LITTLE_ENDIAN)
    ENSURE(load_i32(buffer) == makeI32(3, 2, 1, 0), "");
    ENSURE(load_i32(buffer + 1) == makeI32(251, 3, 2, 1), "");
    ENSURE(load_i32(buffer + 2) == makeI32(252, 251, 3, 2), "");
    ENSURE(load_i32(buffer + 3) == makeI32(253, 252, 251, 3), "");
    ENSURE(load_i32(buffer + 4) == makeI32(254, 253, 252, 251), "");
    ENSURE(load_i32(buffer + 5) == makeI32(255, 254, 253, 252), "");

    ENSURE(load_u32(buffer) == makeU32(3, 2, 1, 0), "");
    ENSURE(load_u32(buffer + 1) == makeU32(251, 3, 2, 1), "");
    ENSURE(load_u32(buffer + 2) == makeU32(252, 251, 3, 2), "");
    ENSURE(load_u32(buffer + 3) == makeU32(253, 252, 251, 3), "");
    ENSURE(load_u32(buffer + 4) == makeU32(254, 253, 252, 251), "");
    ENSURE(load_u32(buffer + 5) == makeU32(255, 254, 253, 252), "");
#else
    ENSURE(load_i32(buffer) == makeI32(0, 1, 2, 3), "");
    ENSURE(load_i32(buffer + 1) == makeI32(1, 2, 3, 251), "");
    ENSURE(load_i32(buffer + 2) == makeI32(2, 3, 251, 252), "");
    ENSURE(load_i32(buffer + 3) == makeI32(3, 251, 252, 253), "");
    ENSURE(load_i32(buffer + 4) == makeI32(251, 252, 253, 254), "");
    ENSURE(load_i32(buffer + 5) == makeI32(252, 253, 254, 255), "");

    ENSURE(load_u32(buffer) == makeU32(0, 1, 2, 3), "");
    ENSURE(load_u32(buffer + 1) == makeU32(1, 2, 3, 251), "");
    ENSURE(load_u32(buffer + 2) == makeU32(2, 3, 251, 252), "");
    ENSURE(load_u32(buffer + 3) == makeU32(3, 251, 252, 253), "");
    ENSURE(load_u32(buffer + 4) == makeU32(251, 252, 253, 254), "");
    ENSURE(load_u32(buffer + 5) == makUI32(252, 253, 254, 255), "");
#endif


#if defined(COMMON_LITTLE_ENDIAN)
    ENSURE(load_i64(buffer) == makeI64(254, 253, 252, 251, 3, 2, 1, 0), "");
    ENSURE(load_i64(buffer + 1) == makeI64(255, 254, 253, 252, 251, 3, 2, 1), "");

    ENSURE(load_u64(buffer) == makeU64(254, 253, 252, 251, 3, 2, 1, 0), "");
    ENSURE(load_u64(buffer + 1) == makeU64(255, 254, 253, 252, 251, 3, 2, 1), "");
#else
    ENSURE(load_i64(buffer) == makeI64(0, 1, 2, 3, 251, 252, 254, 254), "");
    ENSURE(load_i64(buffer + 1) == makeI64(1, 2, 3, 251, 252, 254, 254, 255), "");

    ENSURE(load_u64(buffer) == makeU64(0, 1, 2, 3, 251, 252, 254, 254), "");
    ENSURE(load_u64(buffer + 1) == makeU64(1, 2, 3, 251, 252, 254, 254, 255), "");
#endif

    char loadStoreBuffer[11];
    for (int offset = 0; offset < 3; offset++) {
        int8_t i1 = 0x12;
        store_i8(loadStoreBuffer + offset, i1);
        ENSURE(load_i8(loadStoreBuffer + offset) == i1, "");

        uint8_t u1 = 0xAB;
        store_u8(loadStoreBuffer + offset, u1);
        ENSURE(load_u8(loadStoreBuffer + offset) == u1, "");

        int16_t i2 = 0x1234;
        store_i16(loadStoreBuffer + offset, i2);
        ENSURE(load_i16(loadStoreBuffer + offset) == i2, "");

        uint16_t u2 = 0xABCD;
        store_u16(loadStoreBuffer + offset, u2);
        ENSURE(load_u16(loadStoreBuffer + offset) == u2, "");

        int32_t i3 = 0x12345678;
        store_i32(loadStoreBuffer + offset, i3);
        ENSURE(load_i32(loadStoreBuffer + offset) == i3, "");

        uint32_t u3 = 0xABCDEF12;
        store_u64(loadStoreBuffer + offset, u3);
        ENSURE(load_u64(loadStoreBuffer + offset) == u3, "");

        int64_t i4 = 0x12345678ABCDEF00;
        store_i64(loadStoreBuffer + offset, i4);
        ENSURE(load_i64(loadStoreBuffer + offset) == i4, "");

        uint64_t u4 = 0xABCDEFF00FFEDCAB;
        store_u64(loadStoreBuffer + offset, u4);
        ENSURE(load_u64(loadStoreBuffer + offset) == u4, "");

        int aptr;
        store_ptr(loadStoreBuffer + offset, &aptr);
        ENSURE(load_ptr(loadStoreBuffer + offset) == &aptr, "");
    }

    printf("testLoadStores passed\n");
}

void testByteSwap()
{
    int16_t i1 = 0x1234;
    ENSURE(byteSwap(i1) == 0x3412, "");
    uint16_t u1 = 0xA234;
    ENSURE(byteSwap(u1) == 0x34A2, "");

    int32_t i2 = 0x12345678;
    ENSURE(byteSwap(i2) == 0x78563412, "");
    uint32_t u2 = 0xA2345678;
    ENSURE(byteSwap(u2) == 0x785634A2, "");

    int64_t i3 = 0x12345678ABCDEF00;
    ENSURE(byteSwap(i3) == 0xEFCDAB78563412, "");
    uint64_t u3 = 0xA2345678ABCDEFFF;
    ENSURE(byteSwap(u3) == 0xFFEFCDAB785634A2, "");

    printf("testByteSwap passed\n");
}

void testNextAlignedPtr()
{
    alignas(32) char buffer[32];

    ENSURE(nextAlignedPtr<1>(buffer) == buffer, "");
    ENSURE(nextAlignedPtr<2>(buffer) == buffer, "");
    ENSURE(nextAlignedPtr<4>(buffer) == buffer, "");
    ENSURE(nextAlignedPtr<32>(buffer) == buffer, "");

    ENSURE(nextAlignedPtr<1>(buffer + 1) == buffer + 1, "");
    ENSURE(nextAlignedPtr<2>(buffer + 1) == buffer + 2, "");
    ENSURE(nextAlignedPtr<4>(buffer + 1) == buffer + 4, "");
    ENSURE(nextAlignedPtr<32>(buffer + 1) == buffer + 32, "");

    ENSURE(nextAlignedPtr<1>(buffer + 31) == buffer + 31, "");
    ENSURE(nextAlignedPtr<2>(buffer + 31) == buffer + 32, "");
    ENSURE(nextAlignedPtr<4>(buffer + 31) == buffer + 32, "");
    ENSURE(nextAlignedPtr<32>(buffer + 31) == buffer + 32, "");

    printf("testNextAlignedPtr passed\n");
}

void testElapsedMsec()
{
    enableFinegrainedSleep();

    int const kSleepMsec = 25;
    for (int i = 0; i < 10; i++) {
        int64_t startTime = getTimeTicks();
        sleepMsec(kSleepMsec);
        int msec = elapsedMsec(startTime);
        ENSURE(msec >= kSleepMsec - 5 && msec <= kSleepMsec + 5, "");
    }

    printf("testElapsedMsec passed\n");
}

void testSemaphore()
{
    Semaphore s;
    int const kCount = 1000;
    std::thread t1([&] {
        for (int i = 0; i < kCount; i++) {
            s.post();
        }
    });
    bool endFlag = false;
    std::thread t2([&] {
        for (int i = 0; i < kCount; i++) {
            s.wait();
        }
        endFlag = true;
    });

    t1.join();
    t2.join();

    ENSURE(endFlag, "");

    printf("testSemaphore passed\n");
}

void testRandomRange()
{
    // Tests xorshift128 and reduceRange.
    uint32_t state[4] = {0, 0, 0, 0};
    for (int i = 0; i < 10000; i++) {
        uint32_t v = randomRange(state, 10, 20000);
        ENSURE(v >= 10, "");
        ENSURE(v < 20000, "");
    }
    state[0] = 1; state[1] = 2; state[2] = 3; state[3] = 4;
    for (int i = 0; i < 10000; i++) {
        uint32_t v = randomRange(state, 0, 2);
        ENSURE(v < 2, "");
    }
    for (int i = 0; i < 10000; i++) {
        ENSURE(randomRange(state, 0, 1) == 0, "");
    }

    printf("testRandomRange passed\n");
}

void testArraySize()
{
    int a[15];
    ENSURE(arraySize(a) == 15, "");
    printf("testArraySize passed\n");
}

void testRemoveCRef()
{
    static_assert(std::is_same<RemoveCRef<int>::Type, int>::value);
    static_assert(std::is_same<RemoveCRef<int&>::Type, int>::value);
    static_assert(std::is_same<RemoveCRef<const int>::Type, int>::value);
    static_assert(std::is_same<RemoveCRef<const int&>::Type, int>::value);
    static_assert(std::is_same<RemoveCRef<int&&>::Type, int>::value);
    static_assert(std::is_same<RemoveCRef<const int&&>::Type, int>::value);
    printf("testRemoveCRef passed\n");
}

void testSimpleHash()
{
    ENSURE(simpleHash(nullptr, 0) == 0, "");
    // Call simpleHash to catch errors by asan/ubsan.
    printf("Simple hashes: 'abc': %llx, 'abcd': %llx\n",
           (long long)simpleHash("abc", 3), (long long)simpleHash("abcd", 4));

    printf("testSimpleHash passed\n");
}

void testSimpleAverage()
{
    std::vector<int> v1;
    ENSURE(simpleAverage(v1) == 0, "");

    std::vector<int> v2{1};
    ENSURE(simpleAverage(v2) == 1, "");

    std::vector<int> v3{1, 2};
    ENSURE(simpleAverage(v3) == 1, "");

    std::vector<int> v4{1, 2, 3};
    ENSURE(simpleAverage(v4) == 2, "");

    printf("testSimpleAverage passed\n");
}

void testSetContains()
{
    std::unordered_set<int> s{1, 2, 3};
    ENSURE(!setContains(s, 0), "");
    ENSURE(setContains(s, 3), "");

    printf("testSetContains passed\n");
}

void testRemoveIf()
{
    std::vector<int> v{1, 2, 3, 4, 5};

    removeIf(v, [](int i) { return i < 0; });
    ENSURE((v == std::vector<int>{1, 2, 3, 4, 5}), "");

    removeIf(v, [](int i) { return i % 2 == 0; });
    ENSURE((v == std::vector<int>{1, 3, 5}), "");

    removeIf(v, [](int i) { return i >= 1; });
    ENSURE(v.empty(), "");

    printf("testRemoveIf passed\n");
}

int main()
{
    printf("COMMON_INT_BITS: %d, COMMON_LONG_BITS: %d, COMMON_LONG_LONG_BITS: %d,"
           "COMMON_SIZE_T_BITS: %d\n",
           (int)COMMON_INT_BITS, (int)COMMON_LONG_BITS, (int)COMMON_LONG_LONG_BITS,
           (int)COMMON_SIZE_T_BITS);

#if defined(CPU_IS_X86_64)
    printf("CPU_IS_X86_64\n");
#elif defined(CPU_IS_AARCH64)
    printf("CPU_IS_AARCH64\n");
#endif

#if defined(COMMON_LITTLE_ENDIAN)
    printf("COMMON_LITTLE_ENDIAN\n");
#elif defined(COMMON_BIG_ENDIAN)
    printf("COMMON_BIG_ENDIAN\n");
#endif

    testForceInline();
    testNoInline();

    ENSURE(true, "ensure successful");

    testNextLog2();
    testLoadStores();
    testByteSwap();
    testNextAlignedPtr();
    testElapsedMsec();
    testSemaphore();
    testRandomRange();
    testArraySize();
    testRemoveCRef();
    testSimpleHash();
    testSimpleAverage();
    testSetContains();
    testRemoveIf();
}
