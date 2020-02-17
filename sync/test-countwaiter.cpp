#include "common.h"

#include <thread>

#include "countwaiter.h"

void testCountWaiter()
{
    int const kNumIterations = 10000;
    int const kRandomSpinMax = 10000;

    // Use reads from atomic var as a way to avoid loop optimizations.
    std::atomic<bool> doNotOptimize{true};
    // Run two threads: producer and consumer. Both loop for kNumIterations. Consumer sets start[i],
    // producer waits until startIteration[i] is set. Producer spins a bit, writes
    // to producedValue[i] and then posts on waiter[i]. Consumer spins a bit, waits on waiter[i]
    // and reads producedValue[i].
    auto producer = [&doNotOptimize](std::atomic<bool> start[], int producedValue[],
            CountWaiter* waiter[], uint32_t seed)
    {
        uint32_t randomState[4] = {seed, seed, seed, seed};
        for (int i = 0; i < kNumIterations; i++) {
            while (!start[i]);

            int spinFor = randomRange(randomState, 0, kRandomSpinMax);
            for (int s = 0; s < spinFor && doNotOptimize.load(); s++);

            producedValue[i] += i * i;
            waiter[i]->post();
        }
    };

    auto consumer = [&doNotOptimize](std::atomic<bool> start[], int producedValue[],
            CountWaiter* waiter[], int* totalWaits, uint32_t seed, bool deleteWaiter)
    {
        // randomState should be different from producer!
        uint32_t randomState[4] = {seed, seed, seed, seed};
        for (int i = 0; i < kNumIterations; i++) {
            start[i] = true;

            int spinFor = randomRange(randomState, 0, kRandomSpinMax);
            for (int s = 0; s < spinFor && doNotOptimize.load(); s++);

            if (waiter[i]->count() > 0) {
                (*totalWaits)++;
            }
            waiter[i]->wait();
            if (deleteWaiter) {
                delete waiter[i];
            }
            ENSURE(i * i == producedValue[i], "");
        }
    };

    std::atomic<bool> start1[kNumIterations];
    int value1[kNumIterations];
    CountWaiter* waiter1[kNumIterations];
    for (int i = 0; i < kNumIterations; i++) {
        start1[i] = false;
        value1[i] = 0;
        waiter1[i] = new CountWaiter(1);
    }
    int totalWaits1 = 0;
    std::thread producerThread([&] { producer(start1, value1, waiter1, 1234); });
    std::thread consumerThread([&] {
        consumer(start1, value1, waiter1, &totalWaits1, 5678, true);
    });
    producerThread.join();
    consumerThread.join();

    printf("CountWaiter 1-1 tests passed, total waits: %d (of %d)\n", totalWaits1, kNumIterations);

    // Second test: run two producers and two consumers in parallel, write to two different arrays
    // of values but wait on one waiter.
    std::atomic<bool> start2[kNumIterations];
    int value21[kNumIterations];
    int value22[kNumIterations];
    CountWaiter* waiter2[kNumIterations];

    for (int i = 0; i < kNumIterations; i++) {
        start2[i] = false;
        value21[i] = 0;
        value22[i] = 0;
        waiter2[i] = new CountWaiter(2);
    }
    int totalWaits21 = 0;
    int totalWaits22 = 0;
    std::thread producerThread1([&] { producer(start2, value21, waiter2, 12); });
    std::thread producerThread2([&] { producer(start2, value22, waiter2, 34); });
    // We cannot have deleteWaiter == true for any of the consumers, because we do not have sync
    // between them.
    std::thread consumerThread1([&] {
        consumer(start2, value21, waiter2, &totalWaits21, 56, false);
    });
    std::thread consumerThread2([&] {
        consumer(start2, value22, waiter2, &totalWaits22, 78, false);
    });
    producerThread1.join();
    producerThread2.join();
    consumerThread1.join();
    consumerThread2.join();

    for (int i = 0; i < kNumIterations; i++) {
        delete waiter2[i];
    }

    printf("CountWaiter 2-2 tests passed, total waits: %d, %d (of %d)\n", totalWaits21,
           totalWaits22, kNumIterations);
}
