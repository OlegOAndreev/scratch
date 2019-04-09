#include "common.h"

#include <memory>
#include <string>
#include <unordered_set>

// Use the following if the compiler optimizes out the inner loop.
#if defined(__clang__) || defined(__GNUC__)
#define OPTIMIZATION_BARRIER __asm__ __volatile__("" : : : "memory")
#else
#define OPTIMIZATION_BARRIER
#endif

template<typename I>
const char* uselessInt(I v)
{
    if (v % 2 == 0) {
        return "";
    } else {
        return " ";
    }
}

long long itersPerSec(uint64_t iters, uint64_t startTime)
{
    return iters * getTimeFreq() / (getTimeTicks() - startTime);
}

void benchNextLog2()
{
    int const kNumIterations = 100000000;
    int const kNumPrepared = 128;
    size_t preparedData[kNumPrepared];

    uint32_t xsState[4] = {0, 1, 2, 3};
    for (int i = 0; i < kNumPrepared; i++) {
#if COMMON_SIZE_T_BITS == 64
        preparedData[i] = ((size_t)xorshift128(xsState) << 32) | xorshift128(xsState);
#elif COMMON_SIZE_T_BITS == 32
        preparedData[i] = xorshift128(xsState);
#else
#error "Unsupported COMMON_SIZE_T_BITS"
#endif
    }

    uint64_t startTime = getTimeTicks();
    int r = 0;
    for (int i = 0; i < kNumIterations; i++) {
        r += nextLog2(preparedData[i % kNumPrepared]);
    }

    // Print r to prevent optimizing the calls out.
    printf("%lld nextLog2/sec %s\n",
           itersPerSec(kNumIterations, startTime), uselessInt(r));
}

void benchRandom()
{
    int const kNumIterations = 100000000;
    uint32_t state[4] = {0, 1, 2, 3};

    uint64_t startTime = getTimeTicks();
    uint32_t r = 0;
    for (int i = 0; i < kNumIterations; i++) {
        r += randomRange(state, 0, 1000);
    }
    // Print r to prevent optimizing the calls out.
    printf("%lld randomRanges/sec %s\n",
           itersPerSec(kNumIterations, startTime), uselessInt(r));
}

void benchGetTime()
{
    int const kNumIterations = 10000000;
    uint64_t r = 0;

    uint64_t startTime = getTimeTicks();
    for (int i = 0; i < kNumIterations; i++) {
        r += getTimeTicks();
    }

    // Print r to prevent optimizing the calls out.
    printf("%lld getTimeTicks/sec %s\n",
           itersPerSec(kNumIterations, startTime), uselessInt(r));
}

void benchSimpleHashImpl(size_t size)
{
    int numIterations = 1000000000 / size;
    size_t r = 0;

    uint32_t xsState[4] = {0, 1, 2, 3};
    std::unique_ptr<char[]> buf{new char[size]};
    for (size_t i = 0; i < size; i++) {
        buf[i] = xorshift128(xsState) & 0xFF;
    }

    uint64_t startTime = getTimeTicks();
    for (int i = 0; i < numIterations; i++) {
        r += simpleHash(buf.get(), size);
        OPTIMIZATION_BARRIER;
    }

    // Print r to prevent optimizing the calls out.
    int mbPerSec = (int)((numIterations * size * getTimeFreq()
            / ((getTimeTicks() - startTime) * 1024 * 1024)));
    printf("simpleHash(%d): %dMb/sec %s\n", (int)size, mbPerSec, uselessInt(r));
}

void benchSimpleHash()
{
    benchSimpleHashImpl(10);
    benchSimpleHashImpl(100);
    benchSimpleHashImpl(1000);
    benchSimpleHashImpl(1000000);
}

int main(int argc, char** argv)
{
    std::unordered_set<std::string> benchNames;
    for (int i = 1; i < argc; i++) {
        benchNames.insert(argv[i]);
    }

    if (benchNames.empty() || setContains(benchNames, "nextLog2")) {
        benchNextLog2();
    }

    if (benchNames.empty() || setContains(benchNames, "random")) {
        benchRandom();
    }

    if (benchNames.empty() || setContains(benchNames, "getTimeTicks")) {
        benchGetTime();
    }

    if (benchNames.empty() || setContains(benchNames, "simpleHash")) {
        benchSimpleHash();
    }
}
