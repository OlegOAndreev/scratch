#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <sys/time.h>
#elif defined(__linux__)
#include <time.h>
#endif

bool useRandomFrom = false;

void naiveMemcpy(void* dst, const void* src, size_t size);
void naiveSseMemcpy(void* dst, const void* src, size_t size);
void unrolled2xSseMemcpy(void* dst, const void* src, size_t size);
void unrolled4xSseMemcpy(void* dst, const void* src, size_t size);
void repMovsbMemcpy(void* dst, const void* src, size_t size);
void repMovsqMemcpy(void* dst, const void* src, size_t size);
void muslMemcpy(void* dst, const void* src, size_t size);

// * add tests for memcpy correctness

// Guaranteed to get into L1.
const int L1_SIZE = 16 * 1024;
// Guaranteed to get into L2 but not in L1.
const int L2_SIZE = 96 * 1024;
// Guaranteed to get into main memory.
const int MAIN_SIZE = 64 * 1024 * 1024;

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
template <typename T>
size_t shuffleBlock(char* block, size_t blockSize, size_t strideSize, const T& memcpyFunc)
{
    size_t numStrides = blockSize / strideSize;
    if (useRandomFrom) {
        for (size_t to = 0; to < numStrides / 2; to++) {
            size_t from = rand() % (numStrides / 2) + (numStrides / 2);
            memcpyFunc(block + to * strideSize, block + from * strideSize, strideSize);
        }
    } else {
        for (size_t to = 0; to < numStrides / 2; to++) {
            size_t from = to + (numStrides / 2);
            memcpyFunc(block + to * strideSize, block + from * strideSize, strideSize);
        }
    }

    return blockSize / 2;
}

template <typename T>
void benchMemcpy(char* block, size_t blockSize, size_t strideSize, const T& memcpyFunc, const char* memcpyName)
{
    int64_t timeFreq = getTimeFreq();
    int64_t totalBytes = 0;
    size_t numIters = 0;
    int64_t start = getTimeCounter();
    int64_t deltaUsec;
    while (true) {
        totalBytes += shuffleBlock(block, blockSize, strideSize, memcpyFunc);
        numIters++;
        deltaUsec = getTimeCounter() - start;
        if (deltaUsec > timeFreq) {
            break;
        }
    }
    double gbPerSec = double(totalBytes) * timeFreq / (deltaUsec * 1024 * 1024 * 1024);
    if (gbPerSec > 10) {
        printf("%s: block size %d with copy stride %d: %.1f Gb/sec\n", memcpyName, (int)blockSize, (int)strideSize, gbPerSec);
    } else {
        printf("%s: block size %d with copy stride %d: %.2f Gb/sec\n", memcpyName, (int)blockSize, (int)strideSize, gbPerSec);
    }
}

template <typename T, size_t N>
size_t arraySize(T(&)[N])
{
    return N;
}

int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "random")) {
        useRandomFrom = true;
        srand(0);
    }
    
    char* block = new char[MAIN_SIZE];
    for (size_t i = 0; i < MAIN_SIZE; i++) {
        block[i] = i;
    }

    const size_t L1_STRIDES[] = { 8, 16, 128, L1_SIZE / 4, L1_SIZE / 2 };
    for (size_t i = 0; i < arraySize(L1_STRIDES); i++) {
        size_t blockSize = L1_SIZE;
        size_t strideSize = L1_STRIDES[i];
        benchMemcpy(block, blockSize, strideSize, memcpy, "libc");
        benchMemcpy(block, blockSize, strideSize, naiveMemcpy, "naive");
        benchMemcpy(block, blockSize, strideSize, naiveSseMemcpy, "naiveSse");
        benchMemcpy(block, blockSize, strideSize, unrolled2xSseMemcpy, "unrolled2xSseMemcpy");
        benchMemcpy(block, blockSize, strideSize, unrolled4xSseMemcpy, "unrolled4xSseMemcpy");
        benchMemcpy(block, blockSize, strideSize, repMovsbMemcpy, "repMovsbMemcpy");
        benchMemcpy(block, blockSize, strideSize, repMovsqMemcpy, "repMovsqMemcpy");
        // testMemcpy(block, blockSize, strideSize, muslMemcpy, "muslMemcpy");
        printf("\n");
    }

    const size_t L2_STRIDES[] = { 8, 16, 128, L1_SIZE / 2, L2_SIZE / 4, L2_SIZE / 2 };
    for (size_t i = 0; i < arraySize(L2_STRIDES); i++) {
        size_t blockSize = L2_SIZE;
        size_t strideSize = L2_STRIDES[i];
        benchMemcpy(block, blockSize, strideSize, memcpy, "libc");
        benchMemcpy(block, blockSize, strideSize, naiveMemcpy, "naive");
        benchMemcpy(block, blockSize, strideSize, naiveSseMemcpy, "naiveSse");
        benchMemcpy(block, blockSize, strideSize, unrolled2xSseMemcpy, "unrolled2xSseMemcpy");
        benchMemcpy(block, blockSize, strideSize, unrolled4xSseMemcpy, "unrolled4xSseMemcpy");
        benchMemcpy(block, blockSize, strideSize, repMovsbMemcpy, "repMovsbMemcpy");
        benchMemcpy(block, blockSize, strideSize, repMovsqMemcpy, "repMovsqMemcpy");
        // testMemcpy(block, blockSize, strideSize, muslMemcpy, "muslMemcpy");
        printf("\n");
    }

    const size_t MAIN_STRIDES[] = { 8, 16, 128, L1_SIZE / 2, L2_SIZE / 2, MAIN_SIZE / 4, MAIN_SIZE / 2 };
    for (size_t i = 0; i < arraySize(MAIN_STRIDES); i++) {
        size_t blockSize = MAIN_SIZE;
        size_t strideSize = MAIN_STRIDES[i];
        benchMemcpy(block, blockSize, strideSize, memcpy, "libc");
        benchMemcpy(block, blockSize, strideSize, naiveMemcpy, "naive");
        benchMemcpy(block, blockSize, strideSize, naiveSseMemcpy, "naiveSse");
        benchMemcpy(block, blockSize, strideSize, unrolled2xSseMemcpy, "unrolled2xSseMemcpy");
        benchMemcpy(block, blockSize, strideSize, unrolled4xSseMemcpy, "unrolled4xSseMemcpy");
        benchMemcpy(block, blockSize, strideSize, repMovsbMemcpy, "repMovsbMemcpy");
        benchMemcpy(block, blockSize, strideSize, repMovsqMemcpy, "repMovsqMemcpy");
        // testMemcpy(block, blockSize, strideSize, muslMemcpy, "muslMemcpy");
        printf("\n");
    }
    
    int code = 0;
    for (size_t i = 0; i < MAIN_SIZE; i++) {
        code += block[i];
    }
    return code;
}