#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "common.h"

std::atomic<size_t> totalSum{0};

template<typename Value, typename Adder>
NO_INLINE void doSum(int64_t times, Value* value, Adder adder, size_t baseV)
{
    size_t v = baseV;
    for (int64_t i = 0; i < times; i++) {
        v <<= 1;
        // Very simple pseudo-random code to throw off the optimizer.
        if ((v & 1) == 1) {
            adder(value, 1);
        } else {
            adder(value, -1);
        }
    }
    totalSum.fetch_add(v);
}

template<typename Value, typename Adder>
int64_t doMain(int64_t times, int numThreads, const char* name, Value* values, size_t valuesStride,
               int64_t baseDeltaTime, Adder adder)
{
    const size_t baseV = 123;

    for (int i = 0; i < numThreads; i++) {
        values[i * valuesStride] = 0;
    }

    std::atomic<int> flag{0};
    std::vector<std::thread> threads;
    std::atomic<int> stopFlag{0};
    for (int i = 0; i < numThreads - 1; i++) {
        threads.emplace_back([=, &flag, &stopFlag] {
            flag.fetch_add(1);
            doSum(times, &values[(i + 1) * valuesStride], adder, baseV);
            stopFlag.fetch_add(1);
        });
    }

    // Wait until all thes threads start.
    while (flag.load() != numThreads - 1);

    int64_t timeStart = getTimeTicks();
    doSum(times, &values[0], adder, baseV);

    // Wait until all thes threads stop.
    while (stopFlag.load() != numThreads - 1);

    int64_t deltaTime = getTimeTicks() - timeStart;
    if (baseDeltaTime != 0) {
        printf("%lld %s adds per second (%.1f%% from base), ",
               (long long)(times * getTimeFreq() / deltaTime), name, (double)baseDeltaTime * 100 / deltaTime);
    } else {
        printf("%lld %s adds per second, ", (long long)(times * getTimeFreq() / deltaTime), name);
    }
    printf("\tfinal values: ");
    for (int i = 0; i < numThreads; i++) {
        printf("%lld ", (long long)values[i * valuesStride]);
    }
    printf("\n");

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

    // Enough stride (multiplied by sizeof(size_t)) to put values in different cache lines.
    size_t const kStride = 32;
    std::unique_ptr<size_t[]> simpleValues;
    simpleValues.reset(new size_t[numThreads * kStride]);
    std::unique_ptr<std::atomic<size_t>[]> atomicValues;
    atomicValues.reset(new std::atomic<size_t>[numThreads * kStride]);

    auto simpleAdder = [](size_t* value, size_t next) {
        *value += next;
    };

    auto relaxedAtomicAdder = [](std::atomic<size_t>* value, size_t next) {
        value->fetch_add(next, std::memory_order_relaxed);
    };

    auto acqRelAtomicAdder = [](std::atomic<size_t>* value, size_t next) {
        value->fetch_add(next, std::memory_order_acq_rel);
    };

    auto seqCstAtomicAdder = [](std::atomic<size_t>* value, size_t next) {
        value->fetch_add(next, std::memory_order_seq_cst);
    };

    // Run some speedup loop (on e.g. Android you need a few seconds of CPU utilization in order to reach
    // the required performance).
    printf("Speedup loop, ignore the values\n");
    doMain(times, numThreads, "IGNORE", atomicValues.get(), kStride, 0, relaxedAtomicAdder);

    printf("Testing with 1 threads\n");
    int64_t baseDelta = doMain(times, 1, "simple", simpleValues.get(), 1, 0, simpleAdder);
    doMain(times, 1, "relaxed atomic", atomicValues.get(), 1, baseDelta, relaxedAtomicAdder);
    doMain(times, 1, "acqrel atomic", atomicValues.get(), 1, baseDelta, acqRelAtomicAdder);
    doMain(times, 1, "seqcst atomic", atomicValues.get(), 1, baseDelta, seqCstAtomicAdder);

    printf("Testing with %d threads (strided values)\n", numThreads);
    doMain(times, numThreads, "simple", simpleValues.get(), kStride, baseDelta, simpleAdder);
    doMain(times, numThreads, "relaxed atomic", atomicValues.get(), kStride, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "acqrel atomic", atomicValues.get(), kStride, baseDelta, acqRelAtomicAdder);
    doMain(times, numThreads, "seqcst atomic", atomicValues.get(), kStride, baseDelta, seqCstAtomicAdder);

    printf("Testing with %d threads (sequential values)\n", numThreads);
    doMain(times, numThreads, "simple", simpleValues.get(), 1, baseDelta, simpleAdder);
    doMain(times, numThreads, "relaxed atomic", atomicValues.get(), 1, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "acqrel atomic", atomicValues.get(), 1, baseDelta, acqRelAtomicAdder);
    doMain(times, numThreads, "seqcst atomic", atomicValues.get(), 1, baseDelta, seqCstAtomicAdder);

    printf("Testing with %d threads (one value)\n", numThreads);
    doMain(times, numThreads, "simple", simpleValues.get(), 0, baseDelta, simpleAdder);
    doMain(times, numThreads, "relaxed atomic", atomicValues.get(), 0, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "acqrel atomic", atomicValues.get(), 0, baseDelta, relaxedAtomicAdder);
    doMain(times, numThreads, "seqcst atomic", atomicValues.get(), 0, baseDelta, seqCstAtomicAdder);

    return totalSum;
}
