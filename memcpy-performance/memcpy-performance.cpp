#define RTE_MACHINE_CPUFLAG_AVX2
#include "rte_memcpy.h"

#include <algorithm>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <sys/time.h>
#elif defined(__linux__)
#include <time.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#error "Unsupported OS"
#endif

// If true, randomizes the positions of memory to copy from and to (substantially slows things down).
bool useRandomFrom = false;
bool useRandomTo = false;

int64_t getTimeCounter()
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

int64_t getTimeFreq()
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

template <typename T, size_t N>
size_t arraySize(T(&)[N])
{
    return N;
}

void libcMemcpy(char* dst, const char* src, size_t size)
{
    memcpy(dst, src, size);
}

void rteMemcpy(char* dst, const char* src, size_t size)
{
    rte_memcpy(dst, src, size);
}

// MSVC cannot compile the asm source anyway, use it only to verify MinGW results.
#if !defined(_MSC_VER)
bool isAvxSupported() asm("_isAvxSupported");
#else
bool isAvxSupported()
{
    return true;
}
#endif

void naiveSseMemcpyUnrolledV2Cpp(char* dst, const char* src, size_t size);
void naiveAvxMemcpyUnrolledV2Cpp(char* dst, const char* src, size_t size);
void naiveAvxMemcpyUnrolledV3Cpp(char* dst, const char* src, size_t size);
#if !defined(_MSC_VER)
void naiveMemcpy(char* dst, const char* src, size_t size) asm("_naiveMemcpy");
void naiveMemcpyAligned(char* dst, const char* src, size_t size) asm("_naiveMemcpyAligned");
void naiveMemcpyUnrolled(char* dst, const char* src, size_t size) asm("_naiveMemcpyUnrolled");
void naiveSseMemcpy(char* dst, const char* src, size_t size) asm("_naiveSseMemcpy");
void naiveSseMemcpyAligned(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyAligned");
void naiveSseMemcpyUnrolledBody(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyUnrolledBody");
void naiveSseMemcpyUnrolled(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyUnrolled");
void naiveSseMemcpyUnrolledV2(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyUnrolledV2");
void naiveSseMemcpyUnrolledNT(char* dst, const char* src, size_t size) asm("_naiveSseMemcpyUnrolledNT");
void naiveAvxMemcpy(char* dst, const char* src, size_t size) asm("_naiveAvxMemcpy");
void naiveAvxMemcpyUnrolled(char* dst, const char* src, size_t size) asm("_naiveAvxMemcpyUnrolled");
void naiveAvxMemcpyUnrolledV2(char* dst, const char* src, size_t size) asm("_naiveAvxMemcpyUnrolledV2");
void naiveAvxMemcpyUnrolledNT(char* dst, const char* src, size_t size) asm("_naiveAvxMemcpyUnrolledNT");
void repMovsbMemcpy(char* dst, const char* src, size_t size) asm("_repMovsbMemcpy");
void repMovsqMemcpy(char* dst, const char* src, size_t size) asm("_repMovsqMemcpy");
void memcpyFromMusl(char* dst, const char* src, size_t size) asm("_memcpyFromMusl");
void folly_memcpy(char* dst, const char* src, size_t size) asm("_folly_memcpy");
#endif

#define DECLARE_MEMCPY_FUNC(memcpyFunc, avxRequired) \
    { memcpyFunc, #memcpyFunc, avxRequired }

struct {
    void (* func)(char* dst, const char* src, size_t size);
    const char* name;
    bool avxRequired;
} memcpyFuncs[] = {
    DECLARE_MEMCPY_FUNC(libcMemcpy, false),
    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolledV2Cpp, false),
    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolledV2Cpp, true),
    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolledV3Cpp, true),
    DECLARE_MEMCPY_FUNC(rteMemcpy, true),
#if !defined(_MSC_VER)
//    DECLARE_MEMCPY_FUNC(naiveMemcpy, false),
//    DECLARE_MEMCPY_FUNC(naiveMemcpyAligned, false),
//    DECLARE_MEMCPY_FUNC(naiveMemcpyUnrolled, false),
//    DECLARE_MEMCPY_FUNC(naiveSseMemcpy, false),
//    DECLARE_MEMCPY_FUNC(naiveSseMemcpyAligned, false),
//    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolledBody, false),
//    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolled, false),
    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolledV2, false),
//    DECLARE_MEMCPY_FUNC(naiveSseMemcpyUnrolledNT, false),
//    DECLARE_MEMCPY_FUNC(naiveAvxMemcpy, true),
//    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolled, true),
    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolledV2, true),
//    DECLARE_MEMCPY_FUNC(naiveAvxMemcpyUnrolledNT, true),
    DECLARE_MEMCPY_FUNC(repMovsbMemcpy, false),
//    DECLARE_MEMCPY_FUNC(repMovsqMemcpy, false),
    DECLARE_MEMCPY_FUNC(memcpyFromMusl, false),
//    DECLARE_MEMCPY_FUNC(folly_memcpy, true),
#endif
};


// Compute a very simple hash, see: http://www.eecs.harvard.edu/margo/papers/usenix91/paper.ps
size_t simpleHash(const char* s, size_t size)
{
    size_t hash = 0;
    for (const char* it = s, * end = s + size; it != end; ++it)
        hash = *it + (hash << 6) + (hash << 16) - hash;
    return hash;
}

// Tests one memcpy run with given src, dst and size.
template <typename MemcpyFunc>
bool testMemcpyFuncIter(const MemcpyFunc& memcpyFunc, char* dst, char* src, size_t size)
{
    // Red zone is a small amount of space after buffer, used to check it is not touched.
    const size_t REDZONE = 16;

    // Fills the buffer with randomized values.
    for (size_t i = 0; i < size + REDZONE; i++) {
        src[i] = (unsigned char)(rand() % 256);
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
template <typename MemcpyFunc>
bool testMemcpyFuncSize(const MemcpyFunc& memcpyFunc, char* dstBlock, char* srcBlock, size_t size,
                        const char* memcpyName)
{
    printf("== Testing size %d\n", (int)size);

    if ((size_t)labs(dstBlock - srcBlock) < size + 128) {
        printf("INTERNAL ERROR: srcBlock and dstBlock not too far apart\n");
        return false;
    }

    for (int i = 0; i < 2; i++) {
        for (size_t testSize = size; testSize < size + 32; testSize++) {
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
template <typename MemcpyFunc>
bool testMemcpyFunc(const MemcpyFunc& memcpyFunc, const char* memcpyName)
{
    printf("== Testing memcpy %s\n", memcpyName);

    size_t bigBlockSize = 10 * 1024 * 1024;
    char* bigBlock = new char[bigBlockSize];

    // Test with various sizes. Sizes are specifically chosen to be near power-of-two.
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + bigBlockSize / 2, 1, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + bigBlockSize / 2, 100, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + bigBlockSize / 2, 1000, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + bigBlockSize / 2, 16370, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + bigBlockSize / 2, 131056, memcpyName)) {
        return false;
    }
//    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + MAIN_SIZE / 2, 1048560, memcpyName)) {
//        return false;
//    }

    delete[] bigBlock;
    return true;
}

// Take batches of size blockSize from one half of the block and copy them (randomly or not) to the other half.
template <typename MemcpyFunc>
size_t memcpyBuffer(char* buffer, size_t bufferSize, size_t blockSize, const MemcpyFunc& memcpyFunc)
{
    size_t numBlocks = bufferSize / blockSize;
    // halfBlocks * blockSize * 2 <= numBlocks * blockSize <= bufferSize, so everything stays in bounds.
    size_t halfBlocks = numBlocks / 2;
    if (useRandomFrom && useRandomTo) {
        for (size_t i = 0; i < halfBlocks; i++) {
            size_t from = rand() % halfBlocks + halfBlocks;
            size_t to = rand() % halfBlocks;
            memcpyFunc(buffer + to * blockSize, buffer + from * blockSize, blockSize);
        }
    } else if (useRandomFrom) {
        for (size_t to = 0; to < halfBlocks; to++) {
            size_t from = rand() % halfBlocks + halfBlocks;
            memcpyFunc(buffer + to * blockSize, buffer + from * blockSize, blockSize);
        }
    } else if (useRandomTo) {
        for (size_t from = halfBlocks; from < numBlocks; from++) {
            size_t to = rand() % halfBlocks;
            memcpyFunc(buffer + to * blockSize, buffer + from * blockSize, blockSize);
        }
    } else {
        for (size_t to = 0; to < halfBlocks; to++) {
            size_t from = to + halfBlocks;
            memcpyFunc(buffer + to * blockSize, buffer + from * blockSize, blockSize);
        }
    }

    return halfBlocks * blockSize;
}

// Take blocks of sizes [fromBlockSize ... toBlockSize] from one half of the buffer and copy them (randomly or not)
// to the other half.
template <typename MemcpyFunc>
size_t memcpyBufferMulti(char* buffer, size_t bufferSize, size_t fromBlockSize, size_t toBlockSize,
                         const MemcpyFunc& memcpyFunc)
{
    size_t blockSizes[5];
    blockSizes[0] = fromBlockSize;
    blockSizes[1] = fromBlockSize + (toBlockSize - fromBlockSize) / 4;
    blockSizes[2] = fromBlockSize + (toBlockSize - fromBlockSize) / 2;
    blockSizes[3] = fromBlockSize + (toBlockSize - fromBlockSize) * 3 / 4;
    blockSizes[4] = toBlockSize;

    size_t total = 0;

    for (size_t i = 0; i < arraySize(blockSizes); i++) {
        total += memcpyBuffer(buffer, bufferSize, blockSizes[i], memcpyFunc);
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

template <typename MemcpyFunc>
void benchMemcpy(char* buffer, size_t bufferSize, size_t fromBlockSize, size_t toBlockSize,
                 const MemcpyFunc& memcpyFunc, const char* memcpyName)
{
    int64_t timeFreq = getTimeFreq();
    double gbPerSec[3] = {0.0};
    for (int i = 0; i < 3; i++) {
        int64_t totalBytes = 0;
        int64_t start = getTimeCounter();
        int64_t deltaUsec;
        while (true) {
            totalBytes += memcpyBufferMulti(buffer, bufferSize, fromBlockSize, toBlockSize, memcpyFunc);
            deltaUsec = getTimeCounter() - start;
            if (deltaUsec > timeFreq / 2) {
                break;
            }
        }
        gbPerSec[i] = double(totalBytes) * timeFreq / (deltaUsec * 1024 * 1024 * 1024);
    }
    std::sort(gbPerSec, gbPerSec + arraySize(gbPerSec));

    char memcpyNamePadding[256];
    preparePadding(memcpyNamePadding, memcpyName);
    if (gbPerSec[0] > 10) {
        printf("%s:%s copy block sizes [%d-%d] in buffer size %d: %.1f (%.1f - %.1f) Gb/sec\n", memcpyName,
               memcpyNamePadding, (int)fromBlockSize, (int)toBlockSize, (int)bufferSize, gbPerSec[1], gbPerSec[0],
               gbPerSec[2]);
    } else {
        printf("%s:%s copy block sizes [%d-%d] in buffer size %d: %.2f (%.2f - %.2f) Gb/sec\n", memcpyName,
               memcpyNamePadding, (int)fromBlockSize, (int)toBlockSize, (int)bufferSize, gbPerSec[1], gbPerSec[0],
               gbPerSec[2]);
    }
}

int runBench(size_t bufferSize, const size_t* blockSizes, size_t numBlockSizes)
{
    char* buffer = new char[bufferSize];
    for (size_t i = 0; i < bufferSize; i++) {
        buffer[i] = (char)i;
    }

    for (size_t i = 0; i < numBlockSizes; i++) {
        size_t blockSize = blockSizes[i];
        for (size_t j = 0; j < arraySize(memcpyFuncs); j++) {
            if (memcpyFuncs[j].avxRequired && !isAvxSupported()) {
                continue;
            }
            benchMemcpy(buffer, bufferSize, blockSize, blockSize, memcpyFuncs[j].func, memcpyFuncs[i].name);
        }
        printf("\n");
    }

    int ret = 0;
    for (size_t i = 0; i < bufferSize; i++) {
        ret += buffer[i];
    }
    return ret;
}

int runBenchMulti(size_t bufferSize, const size_t* blockSizes, size_t numBlockSizes)
{
    char* buffer = new char[bufferSize];
    for (size_t i = 0; i < bufferSize; i++) {
        buffer[i] = (char)i;
    }

    for (size_t i = 0; i < numBlockSizes; i++) {
        size_t fromBlockSize = blockSizes[i];
        size_t toBlockSize = fromBlockSize * 2 + 16;
        for (size_t j = 0; j < arraySize(memcpyFuncs); j++) {
            if (memcpyFuncs[j].avxRequired && !isAvxSupported()) {
                continue;
            }
            benchMemcpy(buffer, bufferSize, fromBlockSize, toBlockSize, memcpyFuncs[j].func, memcpyFuncs[j].name);
        }
        printf("\n");
    }

    int ret = 0;
    for (size_t i = 0; i < bufferSize; i++) {
        ret += buffer[i];
    }
    return ret;
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

// Usage:
//   memcpy-test                runs benchmarks for all buffer sizes (L1, L2, MAIN) and all predefined block sizes,
//                              including multi-sized blocks
//   memcpy-test l1             runs benchmarks for L1-sized buffer and all predefined block sizes without multi-sized
//                              blocks
//   memcpy-test l2_multi       runs benchmarks for L1-sized buffer and all predefined multi-sized blocks
//   memcpy-test l1_1000        runs benchmark for L1-sized buffer and block size 1000
//   memcpy-test l2_multi_128   runs benchmark for L2-sized buffer multi-sized block [1000-2016]
//   memcpy-test test           runs correctness tests for all memcpy implementations

// Guaranteed to fit in L1.
const int L1_SIZE = 16 * 1024;
// Guaranteed to fit in L2 but not in L1.
const int L2_SIZE = 96 * 1024;
// Guaranteed to not fit in LLC.
const int MAIN_SIZE = 128 * 1024 * 1024;

size_t L1_BLOCK_SIZES[] = {4, 8, 12, 16, 20, 124, 128, 132, 1020, 1024, 1028,
                           L1_SIZE / 4 - 4, L1_SIZE / 4, L1_SIZE / 4 + 4, L1_SIZE / 2};
size_t L2_BLOCK_SIZES[] = {4, 8, 12, 16, 20, 124, 128, 132, 1020, 1024, 1028,
                           L1_SIZE / 2 - 4, L1_SIZE / 2, L1_SIZE / 2 + 4,
                           L2_SIZE / 4 - 4, L2_SIZE / 4, L2_SIZE / 4 + 4, L2_SIZE / 2};
size_t MAIN_BLOCK_SIZES[] = {4, 8, 12, 16, 20, 124, 128, 132, 1020, 1024, 1028,
                             L1_SIZE / 2 - 4, L1_SIZE / 2, L1_SIZE / 2 + 4,
                             L2_SIZE / 2 - 4, L2_SIZE / 2, L2_SIZE / 2 + 4,
                             MAIN_SIZE / 4 - 4, MAIN_SIZE / 4, MAIN_SIZE / 4 + 4, MAIN_SIZE / 2};

int main(int argc, char** argv)
{
    srand(0);

    bool runTest = false;
    const char* benchName = NULL;
    if (argc > 1) {
        if (strcmp(argv[1], "test") == 0) {
            printf("== Running tests\n");
            runTest = true;
        } else if (strcmp(argv[1], "random") == 0) {
            printf("== Randomized copying enabled\n");
            useRandomFrom = true;
            useRandomTo = true;
            if (argc > 2) {
                benchName = argv[2];
            }
        } else if (strcmp(argv[1], "random-from") == 0) {
            printf("== Randomized gather copying enabled\n");
            useRandomFrom = true;
            if (argc > 2) {
                benchName = argv[2];
            }
        } else if (strcmp(argv[1], "random-to") == 0) {
            printf("== Randomized scatter copying enabled\n");
            useRandomTo = true;
            if (argc > 2) {
                benchName = argv[2];
            }
        } else {
            benchName = argv[1];
        }
    }

    if (isAvxSupported()) {
        printf("== AVX supported\n");
    } else {
        printf("== AVX not supported\n");
    }

    if (runTest) {
        for (size_t i = 0; i < arraySize(memcpyFuncs); i++) {
            if (memcpyFuncs[i].avxRequired && !isAvxSupported()) {
                continue;
            }
            if (!testMemcpyFunc(memcpyFuncs[i].func, memcpyFuncs[i].name)) {
                return 1;
            }
        }
        return 0;
    }

    int code = 0;
    if (benchName != NULL) {
        const char* value = NULL;
        value = stripPrefix(benchName, "l1_multi_");
        if (value != NULL) {
            size_t fixedBlockSize = atoi(value);
            if (fixedBlockSize != 0) {
                code += runBenchMulti(L1_SIZE, &fixedBlockSize, 1);
            }
        }
        value = stripPrefix(benchName, "l1_");
        if (value != NULL) {
            size_t fixedBlockSize = atoi(value);
            if (fixedBlockSize != 0) {
                code += runBench(L1_SIZE, &fixedBlockSize, 1);
            }
        }
        value = stripPrefix(benchName, "l2_multi_");
        if (value != NULL) {
            size_t fixedBlockSize = atoi(value);
            if (fixedBlockSize != 0) {
                code += runBenchMulti(L2_SIZE, &fixedBlockSize, 1);
            }
        }
        value = stripPrefix(benchName, "l2_");
        if (value != NULL) {
            size_t fixedBlockSize = atoi(value);
            if (fixedBlockSize != 0) {
                code += runBench(L2_SIZE, &fixedBlockSize, 1);
            }
        }
        value = stripPrefix(benchName, "main_multi_");
        if (value != NULL) {
            size_t fixedBlockSize = atoi(value);
            if (fixedBlockSize != 0) {
                code += runBenchMulti(MAIN_SIZE, &fixedBlockSize, 1);
            }
        }
        value = stripPrefix(benchName, "main_");
        if (value != NULL) {
            size_t fixedBlockSize = atoi(value);
            if (fixedBlockSize != 0) {
                code += runBench(MAIN_SIZE, &fixedBlockSize, 1);
            }
        }
    }

    if (benchName == NULL || strcmp(benchName, "l1") == 0) {
        code += runBench(L1_SIZE, L1_BLOCK_SIZES, arraySize(L1_BLOCK_SIZES));
    }

    if (benchName == NULL || strcmp(benchName, "l1_multi") == 0) {
        code += runBenchMulti(L1_SIZE, L1_BLOCK_SIZES, arraySize(L1_BLOCK_SIZES) - 4);
    }

    if (benchName == NULL || strcmp(benchName, "l2") == 0) {
        code += runBench(L2_SIZE, L2_BLOCK_SIZES, arraySize(L2_BLOCK_SIZES));
    }

    if (benchName == NULL || strcmp(benchName, "l2_multi") == 0) {
        code += runBenchMulti(L2_SIZE, L2_BLOCK_SIZES, arraySize(L2_BLOCK_SIZES) - 4);
    }

    if (benchName == NULL || strcmp(benchName, "main") == 0) {
        code += runBench(MAIN_SIZE, MAIN_BLOCK_SIZES, arraySize(MAIN_BLOCK_SIZES));
    }

    if (benchName == NULL || strcmp(benchName, "main_multi") == 0) {
        code += runBenchMulti(MAIN_SIZE, MAIN_BLOCK_SIZES, arraySize(MAIN_BLOCK_SIZES) - 4);
    }

    return code;
}
