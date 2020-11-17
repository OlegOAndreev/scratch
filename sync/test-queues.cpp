#include "common.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

#include "blockingqueue.h"
#include "countwaiter.h"
#include "mpmc_bounded_queue/mpmc_bounded_queue.h"
#include "mpscunboundedqueue.h"
#include "stdblockingqueue.h"

template<typename T>
void assertCorrectOut(std::vector<T>& out, int numIters)
{
    if (numIters != (int)out.size()) {
        printf("Output should have %d elements, has %d\n", numIters, (int)out.size());
        exit(1);
    }
    std::sort(out.begin(), out.end());
    for (int i = 0; i < numIters; i++) {
        ENSURE(out[i] == T(i), "");
    }
}

template<typename T, typename Queue>
void runProducerConsumer(Queue& testQueue, int numIters, int numProducers, int numConsumers,
                         bool afterWait, int64_t* pushPerSec, int64_t* popPerSec)
{
    CountWaiter waiter(numProducers);

    std::vector<int> consumerTimes;
    consumerTimes.resize(numConsumers);
    std::vector<std::vector<T>> consumerOut;
    consumerOut.resize(numConsumers);
    for (auto& o : consumerOut) {
        o.reserve(numIters);
    }
    std::vector<std::unique_ptr<std::thread>> consumerThreads;
    for (int i = 0; i < numConsumers; i++) {
        consumerThreads.emplace_back(
            new std::thread([i, afterWait, &testQueue, &waiter, &consumerTimes, &consumerOut] {
                if (afterWait) {
                    waiter.wait();
                }
                uint64_t startTime = getTimeTicks();
                while (true) {
                    T v;
                    if (!testQueue.dequeue(v)) {
                        break;
                    }
                    consumerOut[i].push_back(v);
                }
                consumerTimes[i] = elapsedMsec(startTime);
            }));
    }

    std::vector<int> producerTimes;
    producerTimes.resize(numProducers);
    std::vector<std::unique_ptr<std::thread>> producerThreads;
    for (int i = 0; i < numProducers; i++) {
        producerThreads.emplace_back(
            new std::thread([i, numIters, numProducers, &testQueue, &waiter, &producerTimes] {
                uint64_t startTime = getTimeTicks();
                for (int j = i; j < numIters; j += numProducers) {
                    while (!testQueue.enqueue(T(j))) {
                    }
                }
                producerTimes[i] = elapsedMsec(startTime);
                waiter.post();
            }));
    }
    for (int i = 0; i < numProducers; i++) {
        producerThreads[i]->join();
    }
    testQueue.close();

    int maxProducerTime = producerTimes[0];
    for (int i = 1; i < numProducers; i++) {
        maxProducerTime = std::max(maxProducerTime, producerTimes[i]);
    }
    if (maxProducerTime == 0) {
        maxProducerTime = 1;
    }
    *pushPerSec = (int64_t)numIters * 1000 / maxProducerTime;

    for (int i = 0; i < numConsumers; i++) {
        consumerThreads[i]->join();
    }
    std::vector<T> testOut;
    for (const auto& o : consumerOut) {
        vecAppend(testOut, o);
    }
    assertCorrectOut(testOut, numIters);
    int maxConsumerTime = consumerTimes[0];
    for (int i = 1; i < numConsumers; i++) {
        maxConsumerTime = std::max(maxConsumerTime, consumerTimes[i]);
    }
    if (maxConsumerTime == 0) {
        maxConsumerTime = 1;
    }
    *popPerSec = (int64_t)numIters * 1000 / maxConsumerTime;
}

void computeAndPrintRatio(size_t index, int64_t opPerSec, int64_t const* baselineSpeed,
                          int64_t* newSpeed)
{
    if (baselineSpeed != nullptr) {
        int percent = (baselineSpeed[index] != 0) ? opPerSec * 100 / baselineSpeed[index] : 100;
        if (percent >= 100) {
            printf(" +%d%% to baseline", percent - 100);
        } else {
            printf(" %d%% to baseline", percent - 100);
        }
    }
    if (newSpeed != nullptr) {
        newSpeed[index] = opPerSec;
    }
};

enum class Wait { NO_WAIT, WAIT_ONLY, BOTH };

// T must have a constructor T(int) and have a comparison operator==(T const&, T const&).
template<template<typename> typename Queue, typename T>
void testQueueImpl(const char* typeName, const char* testQueueName, int numIters, int numThreads,
                   bool isMp, bool isMc, Wait wait, int64_t const* baselineSpeed, int64_t* newSpeed)
{
    // Run one producer and one consumer.
    if (wait != Wait::WAIT_ONLY) {
        Queue<T> queue;
        int64_t pushPerSec, popPerSec;
        runProducerConsumer<T>(queue, numIters, 1, 1, false, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements (1 producer) in %s<%s>", (long long)pushPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(0, pushPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("Popped %lld/sec elements (1 consumer) from %s<%s>", (long long)popPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(1, popPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("-----\n");
    }

    // Run one producer and one consumer after the producer finished.
    if (wait != Wait::NO_WAIT) {
        Queue<T> queue;
        int64_t pushPerSec, popPerSec;
        runProducerConsumer<T>(queue, numIters, 1, 1, true, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements (1 producer) in %s<%s>", (long long)pushPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(2, pushPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("Popped %lld/sec elements (1 consumer) from %s<%s> (after wait)",
               (long long)popPerSec, testQueueName, typeName);
        computeAndPrintRatio(3, popPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("-----\n");
    }

    // Run N producers and one consumer.
    if (wait != Wait::WAIT_ONLY && isMp) {
        Queue<T> queue;
        int64_t pushPerSec, popPerSec;
        runProducerConsumer<T>(queue, numIters, numThreads, 1, false, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements (%d producers) in %s<%s>", (long long)pushPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(4, pushPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("Popped %lld/sec elements (1 consumer) from %s<%s>", (long long)popPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(5, popPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("-----\n");
    }

    // Run N producers and one consumer after the producers finished.
    if (wait != Wait::NO_WAIT && isMp) {
        Queue<T> queue;
        int64_t pushPerSec, popPerSec;
        runProducerConsumer<T>(queue, numIters, numThreads, 1, true, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements (%d producers) in %s<%s>", (long long)pushPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(6, pushPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("Popped %lld/sec elements (1 consumer) from %s<%s> (after wait)",
               (long long)popPerSec, testQueueName, typeName);
        computeAndPrintRatio(7, popPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("-----\n");
    }

    // Run N producers and N consumers.
    if (wait != Wait::WAIT_ONLY && isMp && isMc) {
        Queue<T> queue;
        int64_t pushPerSec, popPerSec;
        runProducerConsumer<T>(queue, numIters, numThreads, numThreads, false, &pushPerSec,
                               &popPerSec);
        printf("Pushed %lld/sec elements (%d producers) in %s<%s>", (long long)pushPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(8, pushPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("Popped %lld/sec elements (%d consumers) from %s<%s>", (long long)popPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(9, popPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("-----\n");
    }

    // Run N producers and N consumers after the producers finished.
    if (wait != Wait::NO_WAIT && isMp && isMc) {
        Queue<T> queue;
        int64_t pushPerSec, popPerSec;
        runProducerConsumer<T>(queue, numIters, numThreads, numThreads, true, &pushPerSec,
                               &popPerSec);
        printf("Pushed %lld/sec elements (%d producers) in %s<%s>", (long long)pushPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(10, pushPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("Popped %lld/sec elements (%d consumers) from %s<%s> (after wait)",
               (long long)popPerSec, numThreads, testQueueName, typeName);
        computeAndPrintRatio(11, popPerSec, baselineSpeed, newSpeed);
        printf("\n");
        printf("-----\n");
    }
}

struct FatQueueItem {
    static const size_t kNumInts = 16;
    int data[kNumInts];

    FatQueueItem()
    {
    }

    FatQueueItem(int v)
    {
        for (size_t i = 0; i < kNumInts; i++) {
            data[i] = v;
        }
    }

    bool operator==(FatQueueItem const& other) const
    {
        return std::memcmp(data, other.data, sizeof(data)) == 0;
    }

    bool operator<(FatQueueItem const& other) const
    {
        for (size_t i = 0; i < kNumInts; i++) {
            if (data[i] < other.data[i]) {
                return true;
            } else if (data[i] > other.data[i]) {
                return false;
            }
        }
        return false;
    }
};

int const kIters = 10000000;
size_t const kSmallQueueSize = 1024;
size_t const kBigQueueSize = 1 << nextLog2(kIters);

template<typename T>
struct SmallBlockingMpmcBoundedQueue : public BlockingQueue<mpmc_bounded_queue<T, false>> {
    SmallBlockingMpmcBoundedQueue()
        : BlockingQueue<mpmc_bounded_queue<T, false>>(kSmallQueueSize)
    {
    }
};

template<typename T>
struct BigBlockingMpmcBoundedQueue : public BlockingQueue<mpmc_bounded_queue<T, false>> {
    BigBlockingMpmcBoundedQueue()
        : BlockingQueue<mpmc_bounded_queue<T, false>>(kBigQueueSize)
    {
    }
};

template<typename T>
struct BigBlockingMpmcBoundedQueueWithShuffle : public BlockingQueue<mpmc_bounded_queue<T, true>> {
    BigBlockingMpmcBoundedQueueWithShuffle()
        : BlockingQueue<mpmc_bounded_queue<T, true>>(kBigQueueSize)
    {
    }
};

template<typename T>
using BlockingMpScUnboundedQueue = BlockingQueue<MpScUnboundedQueue<T>>;

// Non-threadsafe deque-based queue to compare the performance with.
template<typename T>
struct StdDeque {
    bool enqueue(T const& t)
    {
        d.push_back(t);
        return true;
    }

    bool dequeue(T& t)
    {
        if (d.empty()) {
            return false;
        }
        t = d.front();
        d.pop_front();
        return true;
    }

    void close()
    {
    }

    bool isClosed() const
    {
        return d.empty();
    }

    std::deque<T> d;
};

// Non-threadsafe stack-based (i.e. unfair) "queue" to compare the performance with.
template<typename T>
struct StdStack {
    bool enqueue(T const& t)
    {
        v.push_back(t);
        return true;
    }

    bool dequeue(T& t)
    {
        if (v.empty()) {
            return false;
        }
        t = v.back();
        v.pop_back();
        return true;
    }

    void close()
    {
    }

    bool isClosed() const
    {
        return v.empty();
    }

    std::vector<T> v;
};

void testQueues(int numThreads)
{
    char queueName[1000];
    {
        printf("Testing int queues\n");

        int64_t baselineSpeedInt[12] = {};

        testQueueImpl<StdBlockingQueue, int>("int", "StdBlockingQueue", kIters, numThreads, true,
                                             true, Wait::BOTH, nullptr, baselineSpeedInt);

        sprintf(queueName, "mpmc_bounded_queue<noshuffle, %d>", (int)kSmallQueueSize);
        testQueueImpl<SmallBlockingMpmcBoundedQueue, int>("int", queueName, kIters, numThreads,
                                                          true, true, Wait::NO_WAIT,
                                                          baselineSpeedInt, nullptr);

        sprintf(queueName, "mpmc_bounded_queue<noshuffle, %d>", (int)kBigQueueSize);
        testQueueImpl<BigBlockingMpmcBoundedQueue, int>("int", queueName, kIters, numThreads, true,
                                                        true, Wait::BOTH, baselineSpeedInt,
                                                        nullptr);

        sprintf(queueName, "mpmc_bounded_queue<shuffle, %d>", (int)kBigQueueSize);
        testQueueImpl<BigBlockingMpmcBoundedQueueWithShuffle, int>(
            "int", queueName, kIters, numThreads, true, true, Wait::BOTH, baselineSpeedInt,
            nullptr);

        testQueueImpl<BlockingMpScUnboundedQueue, int>("int", "MpScUnboundedQueue", kIters,
                                                       numThreads, true, false, Wait::BOTH,
                                                       baselineSpeedInt, nullptr);

        testQueueImpl<StdDeque, int>("int", "std::deque", kIters, numThreads, false, false,
                                     Wait::WAIT_ONLY, baselineSpeedInt, nullptr);

        testQueueImpl<StdStack, int>("int", "std::vector", kIters, numThreads, false, false,
                                     Wait::WAIT_ONLY, baselineSpeedInt, nullptr);

        printf("=====\n");
    }

    {
        printf("Testing FatQueueItem queues\n");

        int64_t baselineSpeedFat[12] = {};

        testQueueImpl<StdBlockingQueue, FatQueueItem>("FatQueueItem", "StdBlockingQueue", kIters,
                                                      numThreads, true, true, Wait::BOTH, nullptr,
                                                      baselineSpeedFat);

        sprintf(queueName, "mpmc_bounded_queue<noshuffle, %d>", (int)kSmallQueueSize);
        testQueueImpl<SmallBlockingMpmcBoundedQueue, FatQueueItem>(
            "FatQueueItem", queueName, kIters, numThreads, true, true, Wait::NO_WAIT,
            baselineSpeedFat, nullptr);

        sprintf(queueName, "mpmc_bounded_queue<noshuffle, %d>", (int)kBigQueueSize);
        testQueueImpl<BigBlockingMpmcBoundedQueue, FatQueueItem>("FatQueueItem", queueName, kIters,
                                                                 numThreads, true, true, Wait::BOTH,
                                                                 baselineSpeedFat, nullptr);

        testQueueImpl<BlockingMpScUnboundedQueue, FatQueueItem>(
            "FatQueueItem", "MpScUnboundedQueue", kIters, numThreads, true, false, Wait::BOTH,
            baselineSpeedFat, nullptr);

        testQueueImpl<StdDeque, FatQueueItem>("FatQueueItem", "std::deque", kIters, numThreads,
                                              false, false, Wait::WAIT_ONLY, baselineSpeedFat,
                                              nullptr);

        testQueueImpl<StdStack, FatQueueItem>("FatQueueItem", "std::vector", kIters, numThreads,
                                              false, false, Wait::WAIT_ONLY, baselineSpeedFat,
                                              nullptr);

        printf("=====\n");
    }
}
