#include "common.h"

#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

#include "blockingqueue.h"
#include "countwaiter.h"
#include "mpscunboundedqueue.h"
#include "stdblockingqueue.h"
#include "mpmc_bounded_queue/mpmc_bounded_queue.h"

// T must have a constructor T(int) and have a comparison operator==(T const&, T const&).
template<typename T, typename Queue>
void testQueueImpl(Queue& testQueue, const char* typeName, const char* testQueueName, int numIters,
                   int64_t const baselineSpeed[8], int64_t newSpeed[8])
{
    // Run several producer and one consumer thread.
    auto producer = [](auto& queue, int start, int end, int step) {
        uint64_t startTime = getTimeTicks();
        for (int i = start; i < end; i += step) {
            ENSURE(queue.enqueue(T(i)), "");
        }
        return elapsedMsec(startTime);
    };

    auto consumer = [](auto& queue, int n, std::vector<T>* out) {
        uint64_t startTime = getTimeTicks();
        out->reserve(n);
        for (int i = 0; i < n; i++) {
            T v;
            queue.dequeue(v);
            out->push_back(v);
        }
        return elapsedMsec(startTime);
    };

    auto assertCorrectOut = [](std::vector<T>& out) {
        std::sort(out.begin(), out.end());
        for (int i = 0, n = out.size(); i < n; i++) {
            ENSURE(out[i] == T(i), "");
        }
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
        int producerTime;
        std::thread producerThread([&] {
            producerTime = producer(testQueue, 0, numIters, 1);
        });
        int consumerTime;
        std::vector<T> testOut;
        std::thread consumerThread([&] {
            consumerTime = consumer(testQueue, numIters, &testOut);
        });
        producerThread.join();
        int64_t pushPerSec = (int64_t)numIters * 1000 / producerTime;
        printf("Pushed %lld/sec elements in %s<%s>", (long long)pushPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(0, pushPerSec);
        printf("\n");

        consumerThread.join();
        assertCorrectOut(testOut);
        int64_t popPerSec = (int64_t)numIters * 1000 / consumerTime;
        printf("Popped %lld/sec elements from %s<%s>", (long long)popPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(1, popPerSec);
        printf("\n");

        printf("-----\n");
    }

    // Run one producer and one consumer after the producer finished.
    {
        CountWaiter waiter(1);
        int producerTime;
        std::thread producerThread([&] {
            producerTime = producer(testQueue, 0, numIters, 1);
            waiter.post();
        });
        int consumerTime;
        std::vector<T> testOut;
        std::thread consumerThread([&] {
            waiter.wait();
            consumerTime = consumer(testQueue, numIters, &testOut);
        });
        producerThread.join();
        int64_t pushPerSec = (int64_t)numIters * 1000 / producerTime;
        printf("Pushed %lld/sec elements in %s<%s>", (long long)pushPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(2, pushPerSec);
        printf("\n");

        consumerThread.join();
        assertCorrectOut(testOut);
        int64_t popPerSec = (int64_t)numIters * 1000 / consumerTime;
        printf("Popped %lld/sec elements from %s<%s> (after delay)", (long long)popPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(3, popPerSec);
        printf("\n");

        printf("-----\n");
    }

    // Run two producers and one consumer.
    {
        int producerTime1, producerTime2;
        std::thread producerThread1([&] {
            producerTime1 = producer(testQueue, 1, numIters + 1, 2);
        });
        std::thread producerThread2([&] {
            producerTime2 = producer(testQueue, 0, numIters, 2);
        });
        int consumerTime;
        std::vector<T> testOut;
        std::thread consumerThread([&] {
            consumerTime = consumer(testQueue, numIters, &testOut);
        });
        producerThread1.join();
        producerThread2.join();
        int64_t pushPerSec = (int64_t)numIters * 1000 / std::max(producerTime1, producerTime2);
        printf("Pushed %lld/sec elements from two threads in %s<%s>", (long long)pushPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(4, pushPerSec);
        printf("\n");

        consumerThread.join();
        assertCorrectOut(testOut);
        int64_t popPerSec = (int64_t)numIters * 1000 / consumerTime;
        printf("Popped %lld/sec elements from %s<%s>", (long long)popPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(5, popPerSec);
        printf("\n");

        printf("-----\n");
    }

    // Run two producers and one consumer after the producers finished.
    {
        CountWaiter waiter(2);
        int producerTime1, producerTime2;
        std::thread producerThread1([&] {
            producerTime1 = producer(testQueue, 1, numIters + 1, 2);
            waiter.post();
        });
        std::thread producerThread2([&] {
            producerTime2 = producer(testQueue, 0, numIters, 2);
            waiter.post();
        });
        int consumerTime;
        std::vector<T> testOut;
        std::thread consumerThread([&] {
            waiter.wait();
            consumerTime = consumer(testQueue, numIters, &testOut);
        });
        producerThread1.join();
        producerThread2.join();
        int64_t pushPerSec = (int64_t)numIters * 1000 / std::max(producerTime1, producerTime2);
        printf("Pushed %lld/sec elements from two threads in %s<%s>", (long long)pushPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(6, pushPerSec);
        printf("\n");

        consumerThread.join();
        assertCorrectOut(testOut);
        int64_t popPerSec = (int64_t)numIters * 1000 / consumerTime;
        printf("Popped %lld/sec elements from %s<%s> (after delay)", (long long)popPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(7, popPerSec);
        printf("\n");

        printf("-----\n");
    }
}

// Large item used for
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

void testQueues()
{
    int const kIters = 10000000;
    size_t const kQueueSize = 1 << nextLog2(kIters);

    {
        printf("Testing int queues\n");

        int64_t baselineSpeedInt[8];

        StdBlockingQueue<int> stdBlockingQueueInt;
        testQueueImpl<int>(stdBlockingQueueInt, "int", "StdBlockingQueue", kIters,
                           nullptr, baselineSpeedInt);

        BlockingQueue<mpmc_bounded_queue<int, true>> mpmcBlockingQueueIntShuffle(kQueueSize);
        testQueueImpl<int>(mpmcBlockingQueueIntShuffle, "int", "mpmc_bounded_queue<shuffle>",
                           kIters, baselineSpeedInt, nullptr);

        BlockingQueue<mpmc_bounded_queue<int, false>> mpmcBlockingQueueIntNoShuffle(kQueueSize);
        testQueueImpl<int>(mpmcBlockingQueueIntNoShuffle, "int", "mpmc_bounded_queue<no shuffle>",
                           kIters, baselineSpeedInt, nullptr);

        BlockingQueue<MpScUnboundedQueue<int>> mpscBlockingQueueInt;
        testQueueImpl<int>(mpscBlockingQueueInt, "int", "MpScUnboundedQueue", kIters,
                           baselineSpeedInt, nullptr);

//        BlockingQueue<MpScUnboundedQueue<int, SimpleAlloc>> mpscBlockingQueueIntAlloc;
//        testQueueImpl<int>(mpscBlockingQueueIntAlloc, "int", "MpScUnboundedQueue+SimpleAlloc",
//                           kIters, baselineSpeedInt, nullptr);

        printf("=====\n");
    }

    {
        printf("Testing FatQueueItem queues\n");

        int64_t baselineSpeedFat[8];

        StdBlockingQueue<FatQueueItem> stdBlockingQueueFat;
        testQueueImpl<FatQueueItem>(stdBlockingQueueFat, "FatQueueItem", "StdBlockingQueue", kIters,
                                    nullptr, baselineSpeedFat);

        BlockingQueue<mpmc_bounded_queue<FatQueueItem>> mpmcBlockingQueueFat(kQueueSize);
        testQueueImpl<FatQueueItem>(mpmcBlockingQueueFat, "FatQueueItem", "mpmc_bounded_queue",
                                    kIters, baselineSpeedFat, nullptr);

        BlockingQueue<MpScUnboundedQueue<FatQueueItem>> mpscBlockingQueueFat;
        testQueueImpl<FatQueueItem>(mpscBlockingQueueFat, "FatQueueItem", "MpScUnboundedQueue",
                                    kIters, baselineSpeedFat, nullptr);

//        BlockingQueue<MpScUnboundedQueue<FatQueueItem, SimpleAlloc>> mpscBlockingQueueFatAlloc;
//        testQueueImpl<FatQueueItem>(mpscBlockingQueueFatAlloc, "FatQueueItem",
//                                    "MpScUnboundedQueue+SimpleAlloc", kIters, baselineSpeedFat,
//                                    nullptr);

        printf("=====\n");
    }
}
