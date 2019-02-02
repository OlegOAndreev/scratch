#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "common.h"

using BaseT = int;

// This is larger than cache line,
const size_t kCacheLineSize = 256;

std::atomic<uint32_t> totalSum{0};

template<typename Value, typename Adder>
NO_INLINE void doSum(int64_t times, Value* value, Value const* nextValue, Adder adder, uint32_t r)
{
    for (int64_t i = 0; i < times; i++) {
        r = (r << 1) | (r >> 31);
        // Very simple pseudo-random code to throw off the optimizer.
        bool add = (r & 1) == 1;
        adder(value, nextValue, add);
    }
    totalSum.fetch_add(r);
}

template<typename Value, typename Adder>
int64_t doMain(int64_t times, int numThreads, const char* name, Value* values, size_t valuesStride, bool fromNext,
               int64_t baseDeltaTime, Adder adder)
{
    if (fromNext && numThreads == 1) {
        return 0;
    }

    const uint32_t r = 123;

    for (int i = 0; i < numThreads; i++) {
        values[i * valuesStride] = 0;
    }


    std::atomic<int> flag{0};
    std::vector<std::thread> threads;
    std::atomic<int> stopFlag{0};
    for (int i = 0; i < numThreads - 1; i++) {
        threads.emplace_back([=, &flag, &stopFlag] {
            flag.fetch_add(1);
            if (fromNext) {
                doSum(times, &values[(i + 1) * valuesStride], &values[((i + 2) % numThreads) * valuesStride], adder, r);
            } else {
                Value dummyValue{1};
                doSum(times, &values[(i + 1) * valuesStride], &dummyValue, adder, r);
            }

            stopFlag.fetch_add(1);
        });
    }

    // Wait until all thes threads start.
    while (flag.load() != numThreads - 1);

    int64_t timeStart = getTimeTicks();
    if (fromNext) {
        doSum(times, &values[0], &values[1], adder, r);
    } else {
        Value dummyValue{1};
        doSum(times, &values[0], &dummyValue, adder, r);
    }

    // Wait until all thes threads stop.
    while (stopFlag.load() != numThreads - 1);

    int64_t deltaTime = getTimeTicks() - timeStart;
    if (baseDeltaTime != 0) {
        printf("%lld %s adds%s per second (%.1f%% from base)",
               (long long)(times * getTimeFreq() / deltaTime), name, fromNext ? " from next" : "",
               (double)baseDeltaTime * 100 / deltaTime);
    } else {
        printf("%lld %s adds%s per second", (long long)(times * getTimeFreq() / deltaTime), name,
               fromNext ? " from next" : "");
    }
    if (fromNext) {
        // There is no sense in printing those values, they are semi-random.
        printf("\n");
    } else {
        printf(", \tfinal values: ");
        for (int i = 0; i < numThreads; i++) {
            printf("%lld ", (long long)values[i * valuesStride]);
        }
        printf("\n");
    }

    for (std::thread& thread : threads) {
        thread.join();
    }

    return deltaTime;
}

int main(int argc, char** argv)
{
    if (argc > 3) {
        printf("Usage: %s [num-thread [times]]\n", argv[0]);
        return 1;
    }

    int64_t times = (int64_t)100000000LL;
    int numThreads = (int)std::thread::hardware_concurrency();
    if (argc >= 2) {
        numThreads = atoi(argv[1]);
    }
    if (argc >= 3) {
        times = atoll(argv[2]);
    }
    printf("Running add %lld times with %d threads\n", (long long)times, (int)numThreads);

    // Enough stride (multiplied by sizeof(BaseT)) to put values in different cache lines.
    size_t const kStride = kCacheLineSize;
    std::unique_ptr<BaseT[]> simpleValues;
    simpleValues.reset(new BaseT[(numThreads + 1) * kStride]);
    BaseT* simpleValuesPtr = nextAlignedPtr<kCacheLineSize>(simpleValues.get());
    std::unique_ptr<std::atomic<BaseT>[]> atomicValues;
    atomicValues.reset(new std::atomic<BaseT>[(numThreads + 1) * kStride]);
    std::atomic<BaseT>* atomicValuesPtr = nextAlignedPtr<kCacheLineSize>(atomicValues.get());

    auto simpleAdder = [](BaseT* value, BaseT const* nextValue, bool add) {
        size_t v = *nextValue;
        *value += add ? v : -v;
    };

    auto relaxedAtomicAdder = [](std::atomic<BaseT>* value, std::atomic<BaseT> const* nextValue, bool add) {
        size_t v = nextValue->load(std::memory_order_relaxed);
        value->fetch_add(add ? v : -v, std::memory_order_relaxed);
    };

    auto acqRelAtomicAdder = [](std::atomic<BaseT>* value, std::atomic<BaseT> const* nextValue, bool add) {
        size_t v = nextValue->load(std::memory_order_acquire);
        value->fetch_add(add ? v : -v, std::memory_order_acq_rel);
    };

    auto seqCstAtomicAdder = [](std::atomic<BaseT>* value, std::atomic<BaseT> const* nextValue, bool add) {
        size_t v = nextValue->load(std::memory_order_seq_cst);
        value->fetch_add(add ? v : -v, std::memory_order_seq_cst);
    };

    // Run some speedup loop (on e.g. Android you need a few seconds of CPU utilization in order to reach
    // the required performance).
    printf("Speedup loop, ignore the values\n");
    doMain(times, numThreads, "IGNORE", atomicValuesPtr, kStride, false, 0, relaxedAtomicAdder);
    printf("++++\n");

    printf("Testing with 1 threads\n----\n");
    int64_t baseDelta = doMain(times, 1, "simple", simpleValuesPtr, 1, false, 0, simpleAdder);
    doMain(times, 1, "relaxed atomic", atomicValuesPtr, 1, false, baseDelta, relaxedAtomicAdder);
    doMain(times, 1, "acqrel atomic", atomicValuesPtr, 1, false, baseDelta, acqRelAtomicAdder);
    doMain(times, 1, "seqcst atomic", atomicValuesPtr, 1, false, baseDelta, seqCstAtomicAdder);
    printf("====\n");

    printf("Testing with %d threads (strided values)\n----\n", numThreads);
    doMain(times, numThreads, "simple", simpleValuesPtr, kStride, false, baseDelta, simpleAdder);
    doMain(times, numThreads, "relaxed atomic", atomicValuesPtr, kStride, false, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "acqrel atomic", atomicValuesPtr, kStride, false, baseDelta, acqRelAtomicAdder);
    doMain(times, numThreads, "seqcst atomic", atomicValuesPtr, kStride, false, baseDelta, seqCstAtomicAdder);
    printf("----\n");
    doMain(times, numThreads, "simple", simpleValuesPtr, kStride, true, baseDelta, simpleAdder);
    doMain(times, numThreads, "relaxed atomic", atomicValuesPtr, kStride, true, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "acqrel atomic", atomicValuesPtr, kStride, true, baseDelta, acqRelAtomicAdder);
    doMain(times, numThreads, "seqcst atomic", atomicValuesPtr, kStride, true, baseDelta, seqCstAtomicAdder);
    printf("====\n");

    printf("Testing with %d threads (sequential values)\n----\n", numThreads);
    doMain(times, numThreads, "simple", simpleValuesPtr, 1, false, baseDelta, simpleAdder);
    doMain(times, numThreads, "relaxed atomic", atomicValuesPtr, 1, false, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "acqrel atomic", atomicValuesPtr, 1, false, baseDelta, acqRelAtomicAdder);
    doMain(times, numThreads, "seqcst atomic", atomicValuesPtr, 1, false, baseDelta, seqCstAtomicAdder);
    printf("----\n");
    doMain(times, numThreads, "simple", simpleValuesPtr, 1, true, baseDelta, simpleAdder);
    doMain(times, numThreads, "relaxed atomic", atomicValuesPtr, 1, true, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "acqrel atomic", atomicValuesPtr, 1, true, baseDelta, acqRelAtomicAdder);
    doMain(times, numThreads, "seqcst atomic", atomicValuesPtr, 1, true, baseDelta, seqCstAtomicAdder);
    printf("====\n");

    printf("Testing with %d threads (one value)\n----\n", numThreads);
    doMain(times, numThreads, "simple", simpleValuesPtr, 0, false, baseDelta, simpleAdder);
    doMain(times, numThreads, "relaxed atomic", atomicValuesPtr, 0, false, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "acqrel atomic", atomicValuesPtr, 0, false, baseDelta, acqRelAtomicAdder);
    doMain(times, numThreads, "seqcst atomic", atomicValuesPtr, 0, false, baseDelta, seqCstAtomicAdder);
    printf("----\n");
    doMain(times, numThreads, "simple", simpleValuesPtr, 0, true, baseDelta, simpleAdder);
    doMain(times, numThreads, "relaxed atomic", atomicValuesPtr, 0, true, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "acqrel atomic", atomicValuesPtr, 0, true, baseDelta, acqRelAtomicAdder);
    doMain(times, numThreads, "seqcst atomic", atomicValuesPtr, 0, true, baseDelta, seqCstAtomicAdder);
    printf("====\n");

    return totalSum;
}
