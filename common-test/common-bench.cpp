#include "common.h"

#include <string>
#include <unordered_set>

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
    printf("%lld nextLog2/sec (ignore this: %d)\n",
           itersPerSec(kNumIterations, startTime), (int)(r % 10));
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
    printf("%lld randomRanges/sec (ignore this: %d)\n",
           itersPerSec(kNumIterations, startTime), (int)(r % 10));
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
}
