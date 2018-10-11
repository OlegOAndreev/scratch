#include <algorithm>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "common.h"

#if defined(CPU_IS_X86_64)
#include <xmmintrin.h>
#elif defined(CPU_IS_AARCH64)
#include <arm_neon.h>
#endif

#if defined(CPU_IS_X86_64)
#define RTE_MACHINE_CPUFLAG_AVX2
#include "rte_memcpy.h"
#endif

// Defines if we have an assembly file with memcpy.
#if defined(__GNUC__)
#define HAS_ASM_MEMCPY
#endif

// Guaranteed to fit in L1.
const size_t L1_SIZE = 16 * 1024;
// Guaranteed to fit in L2 but not in L1.
const size_t L2_SIZE = 96 * 1024;
// Guaranteed to not fit in LLC.
const size_t MAIN_SIZE = 256 * 1024 * 1024;

size_t L1_BLOCK_SIZES[] = {4, 8, 12, 16, 20, 124, 128, 132, 1020, 1024, 1028,
                           L1_SIZE / 4 - 4, L1_SIZE / 4, L1_SIZE / 4 + 4, L1_SIZE / 2};
size_t L1_BLOCK_MULTI_SIZES[] = {4, 8, 12, 16, 20, 124, 128, 132, 1020, 1024, 1028};
size_t L2_BLOCK_SIZES[] = {4, 8, 12, 16, 20, 124, 128, 132, 1020, 1024, 1028,
                           L1_SIZE / 2 - 4, L1_SIZE / 2, L1_SIZE / 2 + 4,
                           L2_SIZE / 4 - 4, L2_SIZE / 4, L2_SIZE / 4 + 4, L2_SIZE / 2};
size_t L2_BLOCK_MULTI_SIZES[] = {4, 8, 12, 16, 20, 124, 128, 132, 1020, 1024, 1028,
                           L1_SIZE / 2 - 4, L1_SIZE / 2, L1_SIZE / 2 + 4};
size_t MAIN_BLOCK_SIZES[] = {4, 8, 12, 16, 20, 124, 128, 132, 1020, 1024, 1028,
                             L1_SIZE / 2 - 4, L1_SIZE / 2, L1_SIZE / 2 + 4,
                             L2_SIZE / 2 - 4, L2_SIZE / 2, L2_SIZE / 2 + 4,
                             MAIN_SIZE / 4 - 4, MAIN_SIZE / 4, MAIN_SIZE / 4 + 4, MAIN_SIZE / 2};
size_t MAIN_BLOCK_MULTI_SIZES[] = {4, 8, 12, 16, 20, 124, 128, 132, 1020, 1024, 1028,
                             L1_SIZE / 2 - 4, L1_SIZE / 2, L1_SIZE / 2 + 4,
                             L2_SIZE / 2 - 4, L2_SIZE / 2, L2_SIZE / 2 + 4};


void libcMemcpy(char* dst, const char* src, size_t size)
{
    memcpy(dst, src, size);
}

#if defined(CPU_IS_X86_64)
void rteMemcpy(char* dst, const char* src, size_t size)
{
    rte_memcpy(dst, src, size);
}
#endif

// MSVC cannot compile the asm source anyway, use it only to verify MinGW results.
#if defined(CPU_IS_X86_64) && defined(HAS_ASM_MEMCPY)
bool isAvxSupported() asm("_isAvxSupported");
#else
bool isAvxSupported()
{
    return false;
}
#endif

void naiveMemcpyUnrolledAlignedCpp(char* dst, const char* src, size_t size);
void naiveMemcpyUnrolledAlignedV2Cpp(char* dst, const char* src, size_t size);
void naiveMemcpyUnrolledAlignedV3Cpp(char* dst, const char* src, size_t size);

#if defined (CPU_IS_X86_64)
void naiveSseMemcpyUnrolledAlignedV2Cpp(char* dst, const char* src, size_t size);
void naiveAvxMemcpyUnrolledAlignedV2Cpp(char* dst, const char* src, size_t size);
void naiveAvxMemcpyUnrolledAlignedV3Cpp(char* dst, const char* src, size_t size);
#endif

#if defined (CPU_IS_AARCH64)
void naiveNeonMemcpyUnrolledAlignedV2Cpp(char* dst, const char* src, size_t size);
#endif

#if defined(HAS_ASM_MEMCPY)
#if defined(CPU_IS_X86_64)
void naiveMemcpy_x86_64(char* dst, const char* src, size_t size) asm("_naiveMemcpy_x86_64");
void naiveMemcpyAligned_x86_64(char* dst, const char* src, size_t size) asm("_naiveMemcpyAligned_x86_64");
void naiveMemcpyUnrolled_x86_64(char* dst, const char* src, size_t size) asm("_naiveMemcpyUnrolled_x86_64");
void naiveSseMemcpy(char* dst, const char* src, size_t size) asm("_naiveSseMemcpy");
void naiveSseMemcpyAligned(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyAligned");
void naiveSseMemcpyUnrolledAlignedBody(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyUnrolledAlignedBody");
void naiveSseMemcpyUnrolledAligned(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyUnrolledAligned");
void naiveSseMemcpyUnrolledAlignedV2(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyUnrolledAlignedV2");
void naiveSseMemcpyUnrolledAlignedV2NT(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyUnrolledAlignedV2NT");
void naiveAvxMemcpyAligned(char* dst, const char* src, size_t size) asm("_naiveAvxMemcpyAligned");
void naiveAvxMemcpyUnrolledAligned(char* dst, const char* src, size_t size) asm("_naiveAvxMemcpyUnrolledAligned");
void naiveAvxMemcpyUnrolledAlignedV2(char* dst, const char* src, size_t size) asm("_naiveAvxMemcpyUnrolledAlignedV2");
void naiveAvxMemcpyUnrolledAlignedV2NT(char* dst, const char* src, size_t size) asm("_naiveAvxMemcpyUnrolledAlignedV2NT");
void repMovsbMemcpy(char* dst, const char* src, size_t size) asm("_repMovsbMemcpy");
void repMovsqMemcpy(char* dst, const char* src, size_t size) asm("_repMovsqMemcpy");
void memcpyFromMusl_x86_64(char* dst, const char* src, size_t size) asm("_memcpyFromMusl_x86_64");
void folly_memcpy(char* dst, const char* src, size_t size) asm("_folly_memcpy");
#elif defined(CPU_IS_AARCH64)
void naiveMemcpy_aarch64(char* dst, const char* src, size_t size) asm("_naiveMemcpy_aarch64");
void naiveMemcpyUnrolledAligned_aarch64(char* dst, const char* src, size_t size) asm("_naiveMemcpyUnrolledAligned_aarch64");
void naiveMemcpyUnrolledAlignedV2_aarch64(char* dst, const char* src, size_t size) asm("_naiveMemcpyUnrolledAlignedV2_aarch64");
void naiveMemcpyUnrolledAlignedV2NeonRegs_aarch64(char* dst, const char* src, size_t size) asm("_naiveMemcpyUnrolledAlignedV2NeonRegs_aarch64");
void naiveMemcpyUnrolledAlignedV3NeonRegs_aarch64(char* dst, const char* src, size_t size) asm("_naiveMemcpyUnrolledAlignedV3NeonRegs_aarch64");
#endif
#endif

// Comment and uncomment array entries to enable or disable particular implementation.
#define DECLARE_MEMCPY_FUNC(memcpyFunc, avxRequired) \
    { memcpyFunc, #memcpyFunc, avxRequired }

using MemcpyFuncType = void (*)(char* dst, const char* src, size_t size);

struct {
    MemcpyFuncType func;
    const char* name;
    bool avxRequired;
} memcpyFuncRegistry[] = {
    DECLARE_MEMCPY_FUNC(libcMemcpy, false),

    DECLARE_MEMCPY_FUNC(naiveMemcpyUnrolledAlignedCpp, false),
    DECLARE_MEMCPY_FUNC(naiveMemcpyUnrolledAlignedV2Cpp, false),
    DECLARE_MEMCPY_FUNC(naiveMemcpyUnrolledAlignedV3Cpp, false),

#if defined(CPU_IS_X86_64)
    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolledAlignedV2Cpp, false),
    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolledAlignedV2Cpp, true),
    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolledAlignedV3Cpp, true),
    DECLARE_MEMCPY_FUNC(rteMemcpy, true),
#endif
#if defined(CPU_IS_AARCH64)
    DECLARE_MEMCPY_FUNC(naiveNeonMemcpyUnrolledAlignedV2Cpp, false),
#endif

#if defined(HAS_ASM_MEMCPY)
#if defined(CPU_IS_X86_64)
    DECLARE_MEMCPY_FUNC(naiveMemcpy_x86_64, false),
    DECLARE_MEMCPY_FUNC(naiveMemcpyAligned_x86_64, false),
    DECLARE_MEMCPY_FUNC(naiveMemcpyUnrolled_x86_64, false),
    DECLARE_MEMCPY_FUNC(naiveSseMemcpy, false),
    DECLARE_MEMCPY_FUNC(naiveSseMemcpyAligned, false),
    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolledAlignedBody, false),
    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolledAligned, false),
    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolledAlignedV2, false),
    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolledAlignedV2NT, false),
    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyAligned, true),
    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolledAligned, true),
    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolledAlignedV2, true),
    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolledAlignedV2NT, true),
    DECLARE_MEMCPY_FUNC(repMovsbMemcpy, false),
    DECLARE_MEMCPY_FUNC(repMovsqMemcpy, false),
    DECLARE_MEMCPY_FUNC(memcpyFromMusl_x86_64, false),
    DECLARE_MEMCPY_FUNC(folly_memcpy, true),
#elif defined(CPU_IS_AARCH64)
    DECLARE_MEMCPY_FUNC(naiveMemcpy_aarch64, false),
    DECLARE_MEMCPY_FUNC(naiveMemcpyUnrolledAligned_aarch64, false),
    DECLARE_MEMCPY_FUNC(naiveMemcpyUnrolledAlignedV2_aarch64, false),
    DECLARE_MEMCPY_FUNC(naiveMemcpyUnrolledAlignedV2NeonRegs_aarch64, false),
    DECLARE_MEMCPY_FUNC(naiveMemcpyUnrolledAlignedV3NeonRegs_aarch64, false),
#endif
#endif
};

// Tests one memcpy run with given src, dst and size.
bool testMemcpyFuncIter(MemcpyFuncType memcpyFunc, char* dst, char* src, size_t size)
{
    // Red zone is a small amount of space after buffer, used to check it is not touched.
    const size_t REDZONE = 16;
    // Completely randomly selected state.
    uint32_t xorstate[4] = { 1, 2, 3, 4 };

    // Fills the buffer with randomized values.
    for (size_t i = 0; i < size + REDZONE; i++) {
        src[i] = (unsigned char)(xorshift128(xorstate) % 256);
    }

    // Computes the input hash to check later that the input has not been modified.
    size_t srcHash = simpleHash(src, size + REDZONE);

    // Fills the space after the dst with different values to check it has not been checked.
    for (size_t i = size; i < size + REDZONE; i++) {
        dst[i] = src[i] ^ 255;
    }

    memcpyFunc(dst, src, size);

    if (memcmp(dst, src, size) != 0) {
        for (size_t i = 0; i < size; i++) {
            if (dst[i] != src[i]) {
                printf("ERROR: Byte %d of %d\n", (int)i, (int)size);
                return false;
            }
        }
        printf("ERROR: What? How did memcmp fail?\n");
        return false;
    }

    if (simpleHash(src, size + REDZONE) != srcHash) {
        printf("ERROR: Input has changed during memcpy\n");
        return false;
    }

    for (size_t i = size; i < size + REDZONE; i++) {
        if ((char)dst[i] != (char)(src[i] ^ 255)) {
            printf("ERROR: Redzone byte %d overwritten (size %d)\n", (int)i, (int)size);
            return false;
        }
    }

    return true;
}

// Tests memcpy with a few sizes near the given size and different alignments (srcBlock and dstBlock must have
// some additional space after them).
bool testMemcpyFuncSize(MemcpyFuncType memcpyFunc, char* dstBlock, char* srcBlock, size_t fromSize, size_t toSize,
                        const char* memcpyName)
{
    printf("== Testing sizes [%d, %d)\n", (int)fromSize, (int)toSize);

    if ((size_t)labs(dstBlock - srcBlock) < toSize + 128) {
        printf("INTERNAL ERROR: srcBlock and dstBlock not too far apart\n");
        return false;
    }

    for (int i = 0; i < 2; i++) {
        for (size_t testSize = fromSize; testSize < toSize; testSize++) {
            for (char* src = srcBlock; src < srcBlock + 16; src++) {
                for (char* dst = dstBlock; dst < dstBlock + 16; dst++) {
                    if (!testMemcpyFuncIter(memcpyFunc, dst, src, testSize)) {
                        printf("ERROR: %s failed on block size %d, src align %d, dst align %d\n", memcpyName,
                               (int)testSize, (int)(src - srcBlock), (int)(dst - dstBlock));
                        return false;
                    }
                }
            }
        }

        // Swap srcBlock and dstBlock so that we check if the switch of direction affects memcpy.
        char* tmp = srcBlock;
        srcBlock = dstBlock;
        dstBlock = tmp;
    }
    return true;
}

// Runs the tests that the memcpy is valid.
bool testMemcpyFunc(MemcpyFuncType memcpyFunc, const char* memcpyName)
{
    printf("== Testing memcpy %s\n", memcpyName);

    size_t bigBlockSize = 10 * 1024 * 1024;
    char* bigBlock = new char[bigBlockSize];

    // Test with various sizes. Sizes are specifically chosen to be near power-of-two.
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + bigBlockSize / 2, 0, 150, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + bigBlockSize / 2, 900, 1100, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + bigBlockSize / 2, (2 << 13) - 10, (2 << 13) + 10, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + bigBlockSize / 2, (2 << 16) - 10, (2 << 16) + 10, memcpyName)) {
        return false;
    }
//    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + MAIN_SIZE / 2, 1048560, memcpyName)) {
//        return false;
//    }

    delete[] bigBlock;
    return true;
}

// Chooses a "random" block size in [minBlockSize, maxBlockSize].
size_t randomBlockSize(size_t minBlockSize, size_t maxBlockSize, uint32_t xorstate[4])
{
    size_t off = xorshift128(xorstate) % 4;
    return minBlockSize + (maxBlockSize - minBlockSize) * off / 4;
}

// Prevent optimizing doIntWork and doSimdWork out.
size_t totalIntWork = 0;
size_t totalSimdWork = 0;

NO_INLINE void doIntWork(const char* buffer, size_t size)
{
    // Do summing of strided values. Use ~cacheline size as a stride
    const size_t stride = 64;
    size_t n = size / stride;
    size_t result = 0;
    for (size_t i = 0; i < n; i++) {
        result += load_u32(buffer + i * stride);
    }
    totalIntWork += result;
}

NO_INLINE void doSimdWork(const char* buffer, size_t size)
{
    // Do summing of strided values. Use ~cacheline size as a stride
    const size_t stride = 64;
    size_t n = size / stride;
#if defined(CPU_IS_X86_64)
    __m128 result = _mm_set1_ps(0.0);
    for (size_t i = 0; i < n; i++) {
        result = _mm_add_ps(result, _mm_load_ss((float*)(buffer + i * stride)));
    }
    totalSimdWork += _mm_cvttss_si32(result);
#elif defined(CPU_IS_AARCH64)
    float32x4_t zero = vdupq_n_f32(0.0);
    float32x4_t result = zero;
    for (size_t i = 0; i < n; i++) {
        result = vaddq_f32(result, vld1q_lane_f32((float*)(buffer + i * stride), zero, 0));
    }
    totalSimdWork += vgetq_lane_s32(vcvtq_s32_f32(result), 0);
#endif
}

// Takes batches of various sizes in [minBlockSize, maxBlockSize] from second half of the buffer and copy them to the first half.
size_t memcpyBuffer(MemcpyFuncType memcpyFunc, char* buffer, size_t bufferSize, size_t minBlockSize, size_t maxBlockSize,
                    bool withIntWork, bool withSimdWork, bool nonRandomAddress)
{
    // Divide buffer in blocks with maxBlockSize, but copy only some random len each frame.
    size_t numBlocks = bufferSize / maxBlockSize;
    // halfBlocks * blockSize * 2 <= numBlocks * blockSize <= bufferSize, so everything stays in bounds.
    size_t halfBlocks = numBlocks / 2;
    // Completely randomly selected state.
    uint32_t xorstate[4] = { 1, 2, 3, 4 };
    size_t total = 0;
    for (size_t i = 0; i < halfBlocks; i++) {
        size_t from;
        size_t to;
        if (nonRandomAddress) {
            from = i + halfBlocks;
            to = i;
        } else {
            from = reduceRange(xorshift128(xorstate), halfBlocks) + halfBlocks;
            to = reduceRange(xorshift128(xorstate), halfBlocks);
        }
        size_t len = randomBlockSize(minBlockSize, maxBlockSize, xorstate);
        memcpyFunc(buffer + to * maxBlockSize, buffer + from * maxBlockSize, len);
        if (withIntWork) {
            doIntWork(buffer, halfBlocks * maxBlockSize);
        }
        if (withSimdWork) {
            doSimdWork(buffer, halfBlocks * maxBlockSize);
        }
        total += len;
    }

    return total;
}

void preparePadding(char* padding, const char* name)
{
    size_t len = strlen(name);
    size_t i = 0;
    if (len < 24) {
        for (; i < 24 - len; i++) {
            padding[i] = ' ';
        }
    }
    padding[i] = '\0';
}

double benchMemcpy(MemcpyFuncType memcpyFunc, const char* memcpyName, char* buffer, size_t bufferSize,
                   size_t minBlockSize, size_t maxBlockSize, bool withIntWork, bool withSimdWork, bool nonRandomAddress,
                   const double* baseSpeed)
{
    int64_t timeFreq = getTimeFreq();
    double gbPerSec[3] = { 0.0 };
    for (int i = 0; i < 3; i++) {
        int64_t totalBytes = 0;
        int64_t start = getTimeTicks();
        int64_t deltaUsec;
        while (true) {
            totalBytes += memcpyBuffer(memcpyFunc, buffer, bufferSize, minBlockSize, maxBlockSize,
                                       withIntWork, withSimdWork, nonRandomAddress);
            deltaUsec = getTimeTicks() - start;
            // Copy for at least half a second.
            if (deltaUsec > timeFreq / 2) {
                break;
            }
        }
        gbPerSec[i] = double(totalBytes) * timeFreq / (deltaUsec * 1024 * 1024 * 1024);
    }
    std::sort(gbPerSec, gbPerSec + arraySize(gbPerSec));

    char memcpyNamePadding[100];
    preparePadding(memcpyNamePadding, memcpyName);

    char description[100];
    sprintf(description, "%s%s%s", nonRandomAddress ? " non-random" : "", withIntWork ? " with int work" : "",
            withSimdWork ? " with simd work" : "");

    char speedStr[100];
    if (gbPerSec[0] > 10) {
        sprintf(speedStr, "%.1f (%.1f - %.1f)", gbPerSec[1], gbPerSec[0], gbPerSec[2]);
    } else {
        sprintf(speedStr, "%.2f (%.2f - %.2f)", gbPerSec[1], gbPerSec[0], gbPerSec[2]);
    }

    double speed = gbPerSec[1];
    // Compare current speed with baseSpeed.
    char relativeSpeedStr[100];
    if (baseSpeed != nullptr) {
        sprintf(relativeSpeedStr, " (%.1f%%)", (speed - *baseSpeed) * 100.0 / *baseSpeed);
    } else {
        strcpy(relativeSpeedStr, "");
    }

    printf("%s:%s copy block sizes [%d-%d] in buffer size %d%s: %s) Gb/sec%s\n", memcpyName, memcpyNamePadding,
           (int)minBlockSize, (int)maxBlockSize, (int)bufferSize, description, speedStr, relativeSpeedStr);
    return speed;
}

void runBench(MemcpyFuncType memcpyFunc, const char* memcpyName, size_t bufferSize,
              const std::vector<size_t>& blockSizes, bool withIntWork, bool withSimdWork, bool nonRandomAddress,
              const std::vector<double>* baseSpeeds, std::vector<double>* speeds)
{
    // Touch each buffer byte before memcpy.
    char* buffer = new char[bufferSize];
    for (size_t i = 0; i < bufferSize; i++) {
        buffer[i] = (char)i;
    }

    speeds->clear();
    for (size_t i = 0; i < blockSizes.size(); i++) {
        const double* baseSpeed = (baseSpeeds != nullptr) ? baseSpeeds->data() + i : nullptr;
        speeds->push_back(benchMemcpy(memcpyFunc, memcpyName, buffer, bufferSize, blockSizes[i], blockSizes[i],
                    withIntWork, withSimdWork, nonRandomAddress, baseSpeed));
    }
}

void runBenchMulti(MemcpyFuncType memcpyFunc, const char* memcpyName, size_t bufferSize,
                   const std::vector<size_t>& blockSizes, bool withIntWork, bool withSimdWork, bool nonRandomAddress,
                   const std::vector<double>* baseSpeeds, std::vector<double>* speeds)
{
    // Touch each buffer byte before memcpy.
    char* buffer = new char[bufferSize];
    for (size_t i = 0; i < bufferSize; i++) {
        buffer[i] = (char)i;
    }

    speeds->clear();
    for (size_t i = 0; i < blockSizes.size(); i++) {
        size_t minBlockSize = blockSizes[i];
        size_t maxBlockSize = minBlockSize * 2 + 16;
        const double* baseSpeed = (baseSpeeds != nullptr) ? baseSpeeds->data() + i : nullptr;
        speeds->push_back(benchMemcpy(memcpyFunc, memcpyName, buffer, bufferSize, minBlockSize, maxBlockSize,
                    withIntWork, withSimdWork, nonRandomAddress, baseSpeed));
    }
}

// If s starts with prefix, returns the pointer to the part after prefix, otherwise returns NULL.
const char* stripPrefix(const char* s, const char* prefix)
{
    size_t prefixLen = strlen(prefix);
    if (strncmp(s, prefix, prefixLen) == 0) {
        return s + prefixLen;
    } else {
        return NULL;
    }
}

void printUsage(const char* argv0)
{
    printf("Usage: %s [--size SIZE] [--align ALIGN] [--with-int-work] [--with-simd-work] [--non-random-address] [--test] [MEMCPY NAMES...]\n"
           "\t--size SIZE\t\tLimits the tested sizes, possible values:\n"
           "\t\tl1_SIZE\t\tDoes copies inside the L1 cache-sized buffer with given SIZE\n"
           "\t\tl1_multi_SIZE\tDoes copies inside the L1 cache-sized buffer with multiple sizes near the SIZE\n"
           "\t\tl2_SIZE\t\tDoes copies inside the L2 cache-sized buffer with multiple sizes near the SIZE\n"
           "\t\tl2_multi_SIZE\tDoes copies inside the L2 cache-sized buffer with multiple sizes near the SIZE\n"
           "\t\tmain_SIZE\tDoes copies inside the buffer larger than LLC with multiple sizes near the SIZE\n"
           "\t\tmain_multi_SIZE\tDoes copies inside the buffer larger than LLC with multiple sizes near the SIZE\n"
           "\t--with-int-work\t\tDo some simple integer work after each memcpy (hash some elements of data)\n"
           "\t--with-simd-work\t\tDo some simple simd work after each memcpy (load and sum some vectors from data)\n"
           "\t--non-random-address\tDo not randomize src and dst addresses when running each memcpy,"
           " is not a very realistic workload\n"
           "\t--test\t\t\tRuns tests on memcpy, validating that memcpy does, in fact, copy memory,"
           " ignores every other argument except memcpy names\n",
           argv0);
}

void parseSize(const char* arg, size_t* bufferSize, std::vector<size_t>* blockSizes, bool* blockMultiSize)
{
    if (strcmp(arg, "l1") == 0) {
        *bufferSize = L1_SIZE;
        blockSizes->assign(L1_BLOCK_SIZES, L1_BLOCK_SIZES + arraySize(L1_BLOCK_SIZES));
        *blockMultiSize = false;
        return;
    }
    if (strcmp(arg, "l1_multi") == 0) {
        *bufferSize = L1_SIZE;
        blockSizes->assign(L1_BLOCK_MULTI_SIZES, L1_BLOCK_MULTI_SIZES + arraySize(L1_BLOCK_MULTI_SIZES));
        *blockMultiSize = true;
        return;
    }
    if (strcmp(arg, "l2") == 0) {
        *bufferSize = L2_SIZE;
        blockSizes->assign(L2_BLOCK_SIZES, L2_BLOCK_SIZES + arraySize(L2_BLOCK_SIZES));
        *blockMultiSize = false;
        return;
    }
    if (strcmp(arg, "l2_multi") == 0) {
        *bufferSize = L2_SIZE;
        blockSizes->assign(L2_BLOCK_MULTI_SIZES, L2_BLOCK_MULTI_SIZES + arraySize(L2_BLOCK_MULTI_SIZES));
        *blockMultiSize = true;
        return;
    }
    if (strcmp(arg, "main") == 0) {
        *bufferSize = MAIN_SIZE;
        blockSizes->assign(MAIN_BLOCK_SIZES, MAIN_BLOCK_SIZES + arraySize(MAIN_BLOCK_SIZES));
        *blockMultiSize = false;
        return;
    }
    if (strcmp(arg, "main_multi") == 0) {
        *bufferSize = MAIN_SIZE;
        blockSizes->assign(MAIN_BLOCK_MULTI_SIZES, MAIN_BLOCK_MULTI_SIZES + arraySize(MAIN_BLOCK_MULTI_SIZES));
        *blockMultiSize = true;
        return;
    }

    const char* value = NULL;
    value = stripPrefix(arg, "l1_multi_");
    if (value != NULL) {
        *bufferSize = L1_SIZE;
        blockSizes->push_back(atoi(value));
        *blockMultiSize = true;
        return;
    }
    value = stripPrefix(arg, "l1_");
    if (value != NULL) {
        *bufferSize = L1_SIZE;
        blockSizes->push_back(atoi(value));
        *blockMultiSize = false;
        return;
    }
    value = stripPrefix(arg, "l2_multi_");
    if (value != NULL) {
        *bufferSize = L2_SIZE;
        blockSizes->push_back(atoi(value));
        *blockMultiSize = true;
        return;
    }
    value = stripPrefix(arg, "l2_");
    if (value != NULL) {
        *bufferSize = L2_SIZE;
        blockSizes->push_back(atoi(value));
        *blockMultiSize = false;
        return;
    }
    value = stripPrefix(arg, "main_multi_");
    if (value != NULL) {
        *bufferSize = MAIN_SIZE;
        blockSizes->push_back(atoi(value));
        *blockMultiSize = true;
        return;
    }
    value = stripPrefix(arg, "main_");
    if (value != NULL) {
        *bufferSize = MAIN_SIZE;
        blockSizes->push_back(atoi(value));
        *blockMultiSize = false;
        return;
    }
}

MemcpyFuncType findMemcpyByName(const char* name)
{
    for (size_t i = 0; i < arraySize(memcpyFuncRegistry); i++) {
        if (strcmp(memcpyFuncRegistry[i].name, name) == 0) {
            if (memcpyFuncRegistry[i].avxRequired && !isAvxSupported()) {
                printf("%s requires AVX, but it is not supported\n", name);
                exit(1);
            }
            return memcpyFuncRegistry[i].func;
        }
    }
    printf("%s is not a known memcpy\n", name);
    exit(1);
}

void listAllMemcpys(std::vector<MemcpyFuncType>* memcpys, std::vector<const char*>* memcpyNames)
{
    bool canAvx = isAvxSupported();
    for (size_t i = 0; i < arraySize(memcpyFuncRegistry); i++) {
        if (memcpyFuncRegistry[i].avxRequired && !canAvx) {
            continue;
        }
        memcpys->push_back(memcpyFuncRegistry[i].func);
        memcpyNames->push_back(memcpyFuncRegistry[i].name);
    }
}

// Computes a diff metric between baselineSpeeds and speeds.
double computeSpeedup(const std::vector<size_t>& blockSizes, const std::vector<double>& baselineSpeeds,
                      const std::vector<double>& speeds)
{
    if (blockSizes.size() != baselineSpeeds.size() || baselineSpeeds.size() != speeds.size()) {
        printf("Failed preconditions\n");
        exit(1);
    }

    double result = 0;
    for (size_t i = 0; i < blockSizes.size(); i++) {
        double sizeSpeedup = (speeds[i] - baselineSpeeds[i]) / baselineSpeeds[i];
        // Arbitrarily chosen weights and thresholds.
        if (blockSizes[i] < 128) {
            result += 0.25 * sizeSpeedup;
        } else if (blockSizes[i] < 1024) {
            result += 0.5 * sizeSpeedup;
        } else {
            result += sizeSpeedup;
        }
    }
    result /= blockSizes.size();
    return result;
}

template<typename T>
void findTwoMaxIndices(const std::vector<T>& values, size_t* maxIndex, size_t* nextMaxIndex)
{
    if (values.size() < 1) {
        printf("Trying to find max values in empty array\n");
        exit(1);
    }
    if (values.size() < 2) {
        *maxIndex = 0;
        *nextMaxIndex = 0;
        return;
    }

    T max1;
    T max2;
    size_t maxIdx1;
    size_t maxIdx2;
    if (values[0] > values[1]) {
        maxIdx1 = 0;
        maxIdx2 = 1;
    } else {
        maxIdx1 = 1;
        maxIdx2 = 0;
    }
    max1 = values[maxIdx1];
    max2 = values[maxIdx2];
    for (size_t i = 2; i < values.size(); i++) {
        if (values[i] > max1) {
            maxIdx2 = maxIdx1;
            maxIdx1 = i;
            max2 = max1;
            max1 = values[i];
        } else if (values[i] > max2) {
            maxIdx2 = i;
            max2 = values[i];
        }
    }
    *maxIndex = maxIdx1;
    *nextMaxIndex = maxIdx2;
}

int main(int argc, char** argv)
{
    size_t bufferSize = 0;
    std::vector<size_t> blockSizes;
    bool blockMultiSize = false;
    bool withIntWork = false;
    bool withSimdWork = false;
    bool nonRandomAddress = false;
    bool runTest = false;
    std::vector<MemcpyFuncType> memcpys;
    std::vector<const char*> memcpyNames;

    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        printUsage(argv[0]);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--size") == 0) {
            parseSize(argv[i + 1], &bufferSize, &blockSizes, &blockMultiSize);
            i++;
        } else if (strcmp(argv[i], "--with-int-work") == 0) {
            withIntWork = true;
        } else if (strcmp(argv[i], "--with-simd-work") == 0) {
            withSimdWork = true;
        } else if (strcmp(argv[i], "--non-random-address") == 0) {
            nonRandomAddress = true;
        } else if (strcmp(argv[i], "--test") == 0) {
            runTest = true;
        } else if (argv[i][0] == '0') {
            printf("Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        } else {
            memcpys.push_back(findMemcpyByName(argv[i]));
            memcpyNames.push_back(argv[i]);
        }
    }

    if (isAvxSupported()) {
        printf("== AVX supported\n");
    } else {
        printf("== AVX not supported\n");
    }

    if (memcpys.empty()) {
        listAllMemcpys(&memcpys, &memcpyNames);
    }

    if (runTest) {
        printf("Running tests on %d memcpys\n", (int)memcpys.size());
        for (size_t i = 0; i < memcpys.size(); i++) {
            testMemcpyFunc(memcpys[i], memcpyNames[i]);
        }
        return 0;
    } else {
        if (bufferSize == 0) {
            printf("--size must be specified\n");
            printUsage(argv[0]);
            return 1;
        }

        printf("Running bench on %d memcpys\n", (int)memcpys.size());
        // Each of the arrays is size is blockSizes.size().
        std::vector<double> baselineSpeeds;
        std::vector<double> speeds;
        // The array size is memcpys.size().
        std::vector<double> totalSpeedup;
        if (blockMultiSize) {
            runBenchMulti(memcpys[0], memcpyNames[0], bufferSize, blockSizes, withIntWork, withSimdWork,
                    nonRandomAddress, nullptr, &baselineSpeeds);
        } else {
            runBench(memcpys[0], memcpyNames[0], bufferSize, blockSizes, withIntWork, withSimdWork,
                    nonRandomAddress, nullptr, &baselineSpeeds);
        }
        totalSpeedup.push_back(0);
        printf("\n");
        for (size_t i = 1; i < memcpys.size(); i++) {
            if (blockMultiSize) {
                runBenchMulti(memcpys[i], memcpyNames[i], bufferSize, blockSizes, withIntWork, withSimdWork,
                              nonRandomAddress, &baselineSpeeds, &speeds);
            } else {
                runBench(memcpys[i], memcpyNames[i], bufferSize, blockSizes, withIntWork, withSimdWork,
                         nonRandomAddress, &baselineSpeeds, &speeds);
            }
            double speedup = computeSpeedup(blockSizes, baselineSpeeds, speeds);
            totalSpeedup.push_back(speedup);
            printf("Total speedup: %.1f%%\n\n", speedup * 100);
        }

        size_t bestMemcpyIdx, nextBestMemcpyIdx;
        findTwoMaxIndices(totalSpeedup, &bestMemcpyIdx, &nextBestMemcpyIdx);
        printf("Best memcpy: %s (total speedup: %.1f%%)\n", memcpyNames[bestMemcpyIdx],
               totalSpeedup[bestMemcpyIdx] * 100);
        printf("Next best memcpy: %s (total speedup: %.1f%%)\n", memcpyNames[nextBestMemcpyIdx],
               totalSpeedup[nextBestMemcpyIdx] * 100);

        return totalIntWork + totalSimdWork;
    }
}
