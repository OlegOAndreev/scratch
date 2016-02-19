#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(__APPLE__)
#include <sys/time.h>
#elif defined(__linux__)
#include <time.h>
#endif

void libcMemcpy(void* dst, const void* src, size_t size);
void naiveMemcpy(void* dst, const void* src, size_t size);
void naiveSseMemcpy(void* dst, const void* src, size_t size);
void unrolled2xSseMemcpy(void* dst, const void* src, size_t size);
void unrolled4xSseMemcpy(void* dst, const void* src, size_t size);
void repMovsbMemcpy(void* dst, const void* src, size_t size);
void repMovsqMemcpy(void* dst, const void* src, size_t size);
void muslMemcpy(void* dst, const void* src, size_t size);

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
    char* fromBase;
    char* toBase;
    if (rand() % 2 == 0) {
        fromBase = block;
        toBase = block + blockSize / 2;
    } else {
        fromBase = block + blockSize / 2;
        toBase = block;
    }

    for (size_t from = 0; from < numStrides / 2; from++) {
        size_t to = rand() % (numStrides / 2);
        memcpyFunc(toBase + to * strideSize, fromBase + from * strideSize, strideSize);
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

int main()
{
    srand(0);
    
    char* block = new char[MAIN_SIZE];
    for (size_t i = 0; i < MAIN_SIZE; i++) {
        block[i] = i;
    }

    const size_t L1_STRIDES[] = { 8, 16, 128, L1_SIZE / 4, L1_SIZE / 2 };
    for (size_t i = 0; i < arraySize(L1_STRIDES); i++) {
        benchMemcpy(block, L1_SIZE, L1_STRIDES[i], libcMemcpy, "libc");
        benchMemcpy(block, L1_SIZE, L1_STRIDES[i], naiveMemcpy, "naive");
        benchMemcpy(block, L1_SIZE, L1_STRIDES[i], naiveSseMemcpy, "naiveSse");
        benchMemcpy(block, L1_SIZE, L1_STRIDES[i], unrolled2xSseMemcpy, "unrolled2xSseMemcpy");
        benchMemcpy(block, L1_SIZE, L1_STRIDES[i], unrolled4xSseMemcpy, "unrolled4xSseMemcpy");
        benchMemcpy(block, L1_SIZE, L1_STRIDES[i], repMovsbMemcpy, "repMovsbMemcpy");
        benchMemcpy(block, L1_SIZE, L1_STRIDES[i], repMovsqMemcpy, "repMovsqMemcpy");
        // testMemcpy(block, L1_SIZE, L1_STRIDES[i], muslMemcpy, "muslMemcpy");
        printf("\n");
    }

    const size_t L2_STRIDES[] = { 8, 16, 128, L1_SIZE / 2, L2_SIZE / 4, L2_SIZE / 2 };
    for (size_t i = 0; i < arraySize(L2_STRIDES); i++) {
        benchMemcpy(block, L2_SIZE, L2_STRIDES[i], libcMemcpy, "libc");
        benchMemcpy(block, L2_SIZE, L2_STRIDES[i], naiveMemcpy, "naive");
        benchMemcpy(block, L2_SIZE, L2_STRIDES[i], naiveSseMemcpy, "naiveSse");
        benchMemcpy(block, L2_SIZE, L2_STRIDES[i], unrolled2xSseMemcpy, "unrolled2xSseMemcpy");
        benchMemcpy(block, L2_SIZE, L2_STRIDES[i], unrolled4xSseMemcpy, "unrolled4xSseMemcpy");
        benchMemcpy(block, L2_SIZE, L2_STRIDES[i], repMovsbMemcpy, "repMovsbMemcpy");
        benchMemcpy(block, L2_SIZE, L2_STRIDES[i], repMovsqMemcpy, "repMovsqMemcpy");
        // testMemcpy(block, L2_SIZE, L2_STRIDES[i], muslMemcpy, "muslMemcpy");
        printf("\n");
    }

    const size_t MAIN_STRIDES[] = { 8, 16, 128, L1_SIZE / 2, L2_SIZE / 2, MAIN_SIZE / 4, MAIN_SIZE / 2 };
    for (size_t i = 0; i < arraySize(MAIN_STRIDES); i++) {
        benchMemcpy(block, MAIN_SIZE, MAIN_STRIDES[i], libcMemcpy, "libc");
        benchMemcpy(block, MAIN_SIZE, MAIN_STRIDES[i], naiveMemcpy, "naive");
        benchMemcpy(block, MAIN_SIZE, MAIN_STRIDES[i], naiveSseMemcpy, "naiveSse");
        benchMemcpy(block, MAIN_SIZE, MAIN_STRIDES[i], unrolled2xSseMemcpy, "unrolled2xSseMemcpy");
        benchMemcpy(block, MAIN_SIZE, MAIN_STRIDES[i], unrolled4xSseMemcpy, "unrolled4xSseMemcpy");
        benchMemcpy(block, MAIN_SIZE, MAIN_STRIDES[i], repMovsbMemcpy, "repMovsbMemcpy");
        benchMemcpy(block, MAIN_SIZE, MAIN_STRIDES[i], repMovsqMemcpy, "repMovsqMemcpy");
        // testMemcpy(block, MAIN_SIZE, MAIN_STRIDES[i], muslMemcpy, "muslMemcpy");
        printf("\n");
    }
    
    int code = 0;
    for (size_t i = 0; i < MAIN_SIZE; i++) {
        code += block[i];
    }
    return code;
}