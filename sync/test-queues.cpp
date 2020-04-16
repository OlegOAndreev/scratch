#include "common.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "blockingqueue.h"
#include "countwaiter.h"
#include "mpmc_bounded_queue/mpmc_bounded_queue.h"
#include "mpscunboundedqueue.h"
#include "stdblockingqueue.h"

// T must have a constructor T(int) and have a comparison operator==(T const&, T const&).
template<typename T, typename Queue>
void testQueueImpl(Queue& testQueue, const char* typeName, const char* testQueueName, int numIters,
                   int numThreads, bool isMp, bool isMc, int64_t const baselineSpeed[8],
                   int64_t newSpeed[8])
{
    auto assertCorrectOut = [](std::vector<T>& out) {
        std::sort(out.begin(), out.end());
        for (int i = 0, n = out.size(); i < n; i++) {
            ENSURE(out[i] == T(i), "");
        }
    };

    auto runProducerConsumer = [&](int numProducers, int numConsumers, bool afterWait,
                                   int64_t* pushPerSec, int64_t* popPerSec) {
        CountWaiter waiter(numProducers);
        std::vector<int> producerTimes;
        producerTimes.resize(numProducers);
        std::vector<std::unique_ptr<std::thread>> producerThreads;
        for (int i = 0; i < numProducers; i++) {
            producerThreads.emplace_back(
                new std::thread([i, numIters, numProducers, &testQueue, &waiter, &producerTimes] {
                    uint64_t startTime = getTimeTicks();
                    for (int j = i; j < numIters; j += numProducers) {
                        ENSURE(testQueue.enqueue(T(j)), "");
                    }
                    producerTimes[i] = elapsedMsec(startTime);
                    waiter.post();
                }));
        }

        std::vector<int> consumerTimes;
        consumerTimes.resize(numConsumers);
        // The consumer either dequeues the last element or waits until consumerQuit is true.
        std::atomic<bool> consumerQuit{false};
        T lastElement = T(numIters - 1);
        std::vector<T> testOut;
        testOut.reserve(numIters);
        std::vector<std::unique_ptr<std::thread>> consumerThreads;
        for (int i = 0; i < numConsumers; i++) {
            consumerThreads.emplace_back(
                new std::thread([i, afterWait, &testQueue, &waiter, &consumerQuit, &lastElement,
                                 &consumerTimes, &testOut] {
                    if (afterWait) {
                        waiter.wait();
                    }
                    uint64_t startTime = getTimeTicks();
                    while (!consumerQuit.load(std::memory_order_relaxed)) {
                        T v;
                        if (testQueue.tryDequeue(v)) {
                            testOut.push_back(v);
                            if (v == lastElement) {
                                consumerQuit.store(true, std::memory_order_relaxed);
                                break;
                            }
                        }
                    }
                    consumerTimes[i] = elapsedMsec(startTime);
                }));
        }

        for (int i = 0; i < numProducers; i++) {
            producerThreads[i]->join();
        }
        int maxProducerTime = producerTimes[0];
        for (int i = 1; i < numThreads; i++) {
            maxProducerTime = std::max(maxProducerTime, producerTimes[i]);
        }
        *pushPerSec = (int64_t)numIters * 1000 / maxProducerTime;

        for (int i = 0; i < numConsumers; i++) {
            consumerThreads[i]->join();
        }
        assertCorrectOut(testOut);
        int maxConsumerTime = consumerTimes[0];
        for (int i = 1; i < numConsumers; i++) {
            maxConsumerTime = std::max(maxConsumerTime, consumerTimes[i]);
        }
        *popPerSec = (int64_t)numIters * 1000 / maxConsumerTime;
    };

    auto computeAndPrintRatio = [baselineSpeed, newSpeed](size_t index, int64_t opPerSec) {
        if (baselineSpeed != nullptr) {
            int percent = opPerSec * 100 / baselineSpeed[index];
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

    // Run one producer and one consumer.
    {
        int64_t pushPerSec, popPerSec;
        runProducerConsumer(1, 1, false, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements in %s<%s>", (long long)pushPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(0, pushPerSec);
        printf("\n");
        printf("Popped %lld/sec elements from %s<%s>", (long long)popPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(1, popPerSec);
        printf("\n");
        printf("-----\n");
    }

    // Run one producer and one consumer after the producer finished.
    {
        int64_t pushPerSec, popPerSec;
        runProducerConsumer(1, 1, true, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements in %s<%s>", (long long)pushPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(2, pushPerSec);
        printf("\n");
        printf("Popped %lld/sec elements from %s<%s> (after wait)", (long long)popPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(3, popPerSec);
        printf("\n");
        printf("-----\n");
    }

    // Run N producers and one consumer.
    if (isMp) {
        int64_t pushPerSec, popPerSec;
        runProducerConsumer(numThreads, 1, false, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements from %d threads in %s<%s>", (long long)pushPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(4, pushPerSec);
        printf("\n");
        printf("Popped %lld/sec elements from %s<%s>", (long long)popPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(5, popPerSec);
        printf("\n");
        printf("-----\n");
    }

    // Run N producers and one consumer after the producers finished.
    if (isMp) {
        int64_t pushPerSec, popPerSec;
        runProducerConsumer(numThreads, 1, true, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements from %d threads in %s<%s>", (long long)pushPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(6, pushPerSec);
        printf("\n");
        printf("Popped %lld/sec elements from %s<%s> (after delay)", (long long)popPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(7, popPerSec);
        printf("\n");
        printf("-----\n");
    }

    // Run N producers and N consumers.
    if (isMp && isMc) {
        int64_t pushPerSec, popPerSec;
        runProducerConsumer(numThreads, numThreads, false, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements from %d threads in %s<%s>", (long long)pushPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(8, pushPerSec);
        printf("\n");
        printf("Popped %lld/sec elements by %d threads from %s<%s>", (long long)popPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(9, popPerSec);
        printf("\n");
        printf("-----\n");
    }

    // Run N producers and N consumers after the producers finished.
    if (isMp && isMc) {
        int64_t pushPerSec, popPerSec;
        runProducerConsumer(numThreads, numThreads, true, &pushPerSec, &popPerSec);
        printf("Pushed %lld/sec elements from %d threads in %s<%s>", (long long)pushPerSec,
               numThreads, testQueueName, typeName);
        computeAndPrintRatio(10, pushPerSec);
        printf("\n");
        printf("Popped %lld/sec elements by %d threads from %s<%s> (after delay)",
               (long long)popPerSec, numThreads, testQueueName, typeName);
        computeAndPrintRatio(11, popPerSec);
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

void testQueues(int numThreads)
{
    int const kIters = 10000000;
    size_t const kQueueSize = 1 << nextLog2(kIters);

    {
        printf("Testing int queues\n");

        int64_t baselineSpeedInt[12];

        StdBlockingQueue<int> stdBlockingQueueInt;
        testQueueImpl<int>(stdBlockingQueueInt, "int", "StdBlockingQueue", kIters, numThreads, true,
                           true, nullptr, baselineSpeedInt);

        BlockingQueue<mpmc_bounded_queue<int, true>> mpmcBlockingQueueIntShuffle(kQueueSize);
        testQueueImpl<int>(mpmcBlockingQueueIntShuffle, "int", "mpmc_bounded_queue<shuffle>",
                           kIters, numThreads, true, true, baselineSpeedInt, nullptr);

        BlockingQueue<mpmc_bounded_queue<int, false>> mpmcBlockingQueueIntNoShuffle(kQueueSize);
        testQueueImpl<int>(mpmcBlockingQueueIntNoShuffle, "int", "mpmc_bounded_queue<no shuffle>",
                           kIters, numThreads, true, true, baselineSpeedInt, nullptr);

        BlockingQueue<MpScUnboundedQueue<int>> mpscBlockingQueueInt;
        testQueueImpl<int>(mpscBlockingQueueInt, "int", "MpScUnboundedQueue", kIters, numThreads,
                           true, false, baselineSpeedInt, nullptr);

        printf("=====\n");
    }

    {
        printf("Testing FatQueueItem queues\n");

        int64_t baselineSpeedFat[12];

        StdBlockingQueue<FatQueueItem> stdBlockingQueueFat;
        testQueueImpl<FatQueueItem>(stdBlockingQueueFat, "FatQueueItem", "StdBlockingQueue", kIters,
                                    numThreads, true, true, nullptr, baselineSpeedFat);

        BlockingQueue<mpmc_bounded_queue<FatQueueItem>> mpmcBlockingQueueFat(kQueueSize);
        testQueueImpl<FatQueueItem>(mpmcBlockingQueueFat, "FatQueueItem", "mpmc_bounded_queue",
                                    kIters, numThreads, true, true, baselineSpeedFat, nullptr);

        BlockingQueue<MpScUnboundedQueue<FatQueueItem>> mpscBlockingQueueFat;
        testQueueImpl<FatQueueItem>(mpscBlockingQueueFat, "FatQueueItem", "MpScUnboundedQueue",
                                    kIters, numThreads, true, true, baselineSpeedFat, nullptr);

        printf("=====\n");
    }
}
