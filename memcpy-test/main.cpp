#include <algorithm>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)

#include <sys/time.h>

#elif defined(__linux__)
#include <time.h>
#endif

// If true, randomizes the positions of memory to copy from and to (substantially slows things down).
bool useRandomFromTo = false;

// Page size on all OSes.
const int PAGE_SIZE = 4096;
// Guaranteed to get into L1.
const int L1_SIZE = 16 * 1024;
// Guaranteed to get into L2 but not in L1.
const int L2_SIZE = 96 * 1024;
// Guaranteed to get into main memory.
const int MAIN_SIZE = 64 * 1024 * 1024;

void libcMemcpy(char* dst, const char* src, size_t size);
void naiveMemcpy(char* dst, const char* src, size_t size);
void naiveMemcpyAligned(char* dst, const char* src, size_t size);
void naiveMemcpyUnrolled(char* dst, const char* src, size_t size);
void naiveSseMemcpy(char* dst, const char* src, size_t size);
void naiveSseMemcpyAligned(char* dst, const char* src, size_t size);
void naiveSseMemcpyUnrolledBody(char* dst, const char* src, size_t size);
void naiveSseMemcpyUnrolled(char* dst, const char* src, size_t size);
void naiveSseMemcpyUnrolledNT(char* dst, const char* src, size_t size);
bool isAvxSupported();
void naiveAvxMemcpy(char* dst, const char* src, size_t size);
void naiveAvxMemcpyUnrolled(char* dst, const char* src, size_t size);
void repMovsbMemcpy(char* dst, const char* src, size_t size);
void repMovsqMemcpy(char* dst, const char* src, size_t size);
void memcpyFromMusl(char* dst, const char* src, size_t size);

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
        src[i] = (unsigned char) (rand() % 256);
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
                printf("ERROR: Byte %d of %d\n", (int) i, (int) size);
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
        if ((char) dst[i] != (char) (src[i] ^ 255)) {
            printf("ERROR: Redzone byte %d overwritten (size %d)\n", (int) i, (int) size);
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
    printf("== Testing size %d\n", (int) size);

    if ((size_t) labs(dstBlock - srcBlock) < size + 128) {
        printf("INTERNAL ERROR: srcBlock and dstBlock not too far apart\n");
        return false;
    }

    for (int i = 0; i < 2; i++) {
        for (size_t testSize = size; testSize < size + 32; testSize++) {
            for (char* src = srcBlock; src < srcBlock + 16; src++) {
                for (char* dst = dstBlock; dst < dstBlock + 16; dst++) {
                    if (!testMemcpyFuncIter(memcpyFunc, dst, src, testSize)) {
                        printf("ERROR: %s failed on block size %d, src align %d, dst align %d\n", memcpyName,
                               (int) testSize, (int) (src - srcBlock), (int) (dst - dstBlock));
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

    char* bigBlock = new char[MAIN_SIZE];

    // Test with various sizes. Sizes are specifically chosen to be near power-of-two.
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + MAIN_SIZE / 2, 1, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + MAIN_SIZE / 2, 100, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + MAIN_SIZE / 2, 1000, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + MAIN_SIZE / 2, 16370, memcpyName)) {
        return false;
    }
    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + MAIN_SIZE / 2, 131056, memcpyName)) {
        return false;
    }
//    if (!testMemcpyFuncSize(memcpyFunc, bigBlock, bigBlock + MAIN_SIZE / 2, 1048560, memcpyName)) {
//        return false;
//    }

    delete[] bigBlock;
    return true;
}

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
#endif
}

int64_t getTimeFreq()
{
#if defined(__APPLE__)
    return 1000000;
#elif defined(__linux__)
    return 1000000000;
#endif
}

// Take strides of size strideSize from one half of the block and randomly copy them to the other half.
template <typename MemcpyFunc>
size_t shuffleBlock(char* block, size_t blockSize, size_t strideSize, const MemcpyFunc& memcpyFunc)
{
    size_t numStrides = blockSize / strideSize;
    // halfStrides * strideSize * 2 <= numStrides * strideSize <= blockSize, so everything stays in bounds.
    size_t halfStrides = numStrides / 2;
    if (useRandomFromTo) {
        for (size_t i = 0; i < halfStrides; i++) {
            size_t from = rand() % halfStrides + halfStrides;
            size_t to = rand() % halfStrides;
            memcpyFunc(block + to * strideSize, block + from * strideSize, strideSize);
        }
    } else {
        for (size_t to = 0; to < halfStrides; to++) {
            size_t from = to + halfStrides;
            memcpyFunc(block + to * strideSize, block + from * strideSize, strideSize);
        }
    }

    return blockSize / 2;
}

// I can't believe it, I'm writing a bubble sort.
template <typename T>
void bubbleSort(T* a, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n - i - 1; j++) {
            if (a[j] > a[j + 1]) {
                T tmp = a[j];
                a[j] = a[j + 1];
                a[j + 1] = tmp;
            }
        }
    }
}

template <typename T, size_t N>
size_t arraySize(T(&)[N])
{
    return N;
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
void benchMemcpy(char* block, size_t blockSize, size_t strideSize, const MemcpyFunc& memcpyFunc, const char* memcpyName)
{
    int64_t timeFreq = getTimeFreq();
    double gbPerSec[3] = {0.0};
    for (int i = 0; i < 3; i++) {
        int64_t totalBytes = 0;
        int64_t start = getTimeCounter();
        int64_t deltaUsec;
        while (true) {
            totalBytes += shuffleBlock(block, blockSize, strideSize, memcpyFunc);
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
        printf("%s:%s block size %d with copy stride %d: %.1f (%.1f - %.1f) Gb/sec\n", memcpyName, memcpyNamePadding,
               (int) blockSize, (int) strideSize, gbPerSec[1], gbPerSec[0], gbPerSec[2]);
    } else {
        printf("%s:%s block size %d with copy stride %d: %.2f (%.2f - %.2f) Gb/sec\n", memcpyName, memcpyNamePadding,
               (int) blockSize, (int) strideSize, gbPerSec[1], gbPerSec[0], gbPerSec[2]);
    }
}

// Gets the next pointer after src aligned to align. If src is already aligned, it is returned.
template <size_t align, typename T>
T* alignPtr(T* src)
{
    static_assert((align & (align - 1)) == 0, "align must be power-of-two");
    return (T*) (((uintptr_t) src + align - 1) & ~(align - 1));
}

// Returns true if name is contained in names or names is empty.
bool containedIn(const char* name, char** names, int numNames)
{
    if (numNames == 0) {
        return true;
    }
    for (int i = 0; i < numNames; i++) {
        if (strcmp(name, names[i]) == 0) {
            return true;
        }
    }
    return false;
}

#define TEST_MEMCPY(memcpyFunc, memcpyNames, numMemcpyNames) \
    if (containedIn(#memcpyFunc, memcpyNames, numMemcpyNames)) { \
        if (!testMemcpyFunc(memcpyFunc, #memcpyFunc)) { \
            return 1; \
        } \
    }

#define BENCH_MEMCPY(block, blockSize, strideSize, memcpyFunc, memcpyNames, numMemcpyNames) \
    if (containedIn(#memcpyFunc, memcpyNames, numMemcpyNames)) { \
        benchMemcpy(block, blockSize, strideSize, memcpyFunc, #memcpyFunc); \
    }

int main(int argc, char** argv)
{
    srand(0);

    bool runTest = false;
    char** memcpyNames;
    int numMemcpyNames;
    if (argc > 1) {
        if (strcmp(argv[1], "random") == 0) {
            printf("== Randomized copying enabled\n");
            useRandomFromTo = true;
            memcpyNames = argv + 2;
            numMemcpyNames = argc - 2;
        } else if (strcmp(argv[1], "test") == 0) {
            printf("== Running tests\n");
            runTest = true;
            memcpyNames = argv + 2;
            numMemcpyNames = argc - 2;
        } else {
            memcpyNames = argv + 1;
            numMemcpyNames = argc - 1;
        }
    }

    bool avxSupported = isAvxSupported();
    if (avxSupported) {
        printf("== AVX supported\n");
    } else {
        printf("== AVX not supported\n");
    }

    if (runTest) {
        TEST_MEMCPY(libcMemcpy, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(naiveMemcpy, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(naiveMemcpyAligned, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(naiveMemcpyUnrolled, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(naiveSseMemcpy, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(naiveSseMemcpyAligned, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(naiveSseMemcpyUnrolled, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(naiveSseMemcpyUnrolledBody, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(naiveSseMemcpyUnrolledNT, memcpyNames, numMemcpyNames)
        if (avxSupported) {
            TEST_MEMCPY(naiveAvxMemcpy, memcpyNames, numMemcpyNames)
            TEST_MEMCPY(naiveAvxMemcpyUnrolled, memcpyNames, numMemcpyNames)
        }
        TEST_MEMCPY(repMovsbMemcpy, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(repMovsqMemcpy, memcpyNames, numMemcpyNames)
        TEST_MEMCPY(memcpyFromMusl, memcpyNames, numMemcpyNames)
        return 0;
    }

    char* block = new char[MAIN_SIZE];
    for (size_t i = 0; i < MAIN_SIZE; i++) {
        block[i] = i;
    }

    const size_t PAGE_STRIDES[] = {4, 8, 12, 16, 20, 124, 128, 132, PAGE_SIZE / 4 - 4, PAGE_SIZE / 4, PAGE_SIZE / 4 + 4,
                                   PAGE_SIZE / 2};
    for (size_t i = 0; i < arraySize(PAGE_STRIDES); i++) {
        size_t blockSize = PAGE_SIZE;
        size_t strideSize = PAGE_STRIDES[i];
        char* alignedBlock = alignPtr<PAGE_SIZE>(block);
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, libcMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveMemcpyAligned, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveMemcpyUnrolled, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveSseMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveSseMemcpyAligned, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveSseMemcpyUnrolledBody, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveSseMemcpyUnrolled, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveSseMemcpyUnrolledNT, memcpyNames, numMemcpyNames)
        if (avxSupported) {
            BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveAvxMemcpy, memcpyNames, numMemcpyNames)
            BENCH_MEMCPY(alignedBlock, blockSize, strideSize, naiveAvxMemcpyUnrolled, memcpyNames, numMemcpyNames)
        }
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, repMovsbMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, repMovsqMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(alignedBlock, blockSize, strideSize, memcpyFromMusl, memcpyNames, numMemcpyNames)
        printf("\n");
    }

    const size_t L1_STRIDES[] = {4, 8, 12, 16, 20, 124, 128, 132, PAGE_SIZE / 4 - 4, PAGE_SIZE / 4, PAGE_SIZE / 4 + 4,
                                 L1_SIZE / 4 - 4, L1_SIZE / 4, L1_SIZE / 4 + 4, L1_SIZE / 2};
    for (size_t i = 0; i < arraySize(L1_STRIDES); i++) {
        size_t blockSize = L1_SIZE;
        size_t strideSize = L1_STRIDES[i];
        BENCH_MEMCPY(block, blockSize, strideSize, libcMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveMemcpyAligned, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveMemcpyUnrolled, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyAligned, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyUnrolledBody, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyUnrolled, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyUnrolledNT, memcpyNames, numMemcpyNames)
        if (avxSupported) {
            BENCH_MEMCPY(block, blockSize, strideSize, naiveAvxMemcpy, memcpyNames, numMemcpyNames)
            BENCH_MEMCPY(block, blockSize, strideSize, naiveAvxMemcpyUnrolled, memcpyNames, numMemcpyNames)
        }
        BENCH_MEMCPY(block, blockSize, strideSize, repMovsbMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, repMovsqMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, memcpyFromMusl, memcpyNames, numMemcpyNames)
        printf("\n");
    }

    const size_t L2_STRIDES[] = {4, 8, 12, 16, 20, 124, 128, 132, PAGE_SIZE / 4 - 4, PAGE_SIZE / 4, PAGE_SIZE / 4 + 4,
                                 L1_SIZE / 2 - 4, L1_SIZE / 2, L1_SIZE / 2 + 4, L2_SIZE / 4 - 4, L2_SIZE / 4,
                                 L2_SIZE / 4 + 4, L2_SIZE / 2};
    for (size_t i = 0; i < arraySize(L2_STRIDES); i++) {
        size_t blockSize = L2_SIZE;
        size_t strideSize = L2_STRIDES[i];
        BENCH_MEMCPY(block, blockSize, strideSize, libcMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveMemcpyAligned, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveMemcpyUnrolled, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyAligned, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyUnrolledBody, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyUnrolled, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyUnrolledNT, memcpyNames, numMemcpyNames)
        if (avxSupported) {
            BENCH_MEMCPY(block, blockSize, strideSize, naiveAvxMemcpy, memcpyNames, numMemcpyNames)
            BENCH_MEMCPY(block, blockSize, strideSize, naiveAvxMemcpyUnrolled, memcpyNames, numMemcpyNames)
        }
        BENCH_MEMCPY(block, blockSize, strideSize, repMovsbMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, repMovsqMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, memcpyFromMusl, memcpyNames, numMemcpyNames)
        printf("\n");
    }

    const size_t MAIN_STRIDES[] = {4, 8, 12, 16, 20, 124, 128, 132, PAGE_SIZE / 4 - 4, PAGE_SIZE / 4, PAGE_SIZE / 4 + 4,
                                   L1_SIZE / 2 - 4, L1_SIZE / 2, L1_SIZE / 2 + 4, L2_SIZE / 2 - 4, L2_SIZE / 2,
                                   L2_SIZE / 2 + 4, MAIN_SIZE / 4 - 4, MAIN_SIZE / 4, MAIN_SIZE / 4 + 4, MAIN_SIZE / 2};
    for (size_t i = 0; i < arraySize(MAIN_STRIDES); i++) {
        size_t blockSize = MAIN_SIZE;
        size_t strideSize = MAIN_STRIDES[i];
        BENCH_MEMCPY(block, blockSize, strideSize, libcMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveMemcpyAligned, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveMemcpyUnrolled, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyAligned, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyUnrolledBody, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyUnrolled, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, naiveSseMemcpyUnrolledNT, memcpyNames, numMemcpyNames)
        if (avxSupported) {
            BENCH_MEMCPY(block, blockSize, strideSize, naiveAvxMemcpy, memcpyNames, numMemcpyNames)
            BENCH_MEMCPY(block, blockSize, strideSize, naiveAvxMemcpyUnrolled, memcpyNames, numMemcpyNames)
        }
        BENCH_MEMCPY(block, blockSize, strideSize, repMovsbMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, repMovsqMemcpy, memcpyNames, numMemcpyNames)
        BENCH_MEMCPY(block, blockSize, strideSize, memcpyFromMusl, memcpyNames, numMemcpyNames)
        printf("\n");
    }

    int code = 0;
    for (size_t i = 0; i < MAIN_SIZE; i++) {
        code += block[i];
    }
    return code;
}
