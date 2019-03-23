#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "countwaiter.h"
#include "fiber.h"
#include "fiberworkstealingpool.h"
#include "fixedfunction.h"
#include "futureutils.h"
#include "blockingqueue.h"
#include "mpscunboundedqueue.h"
#include "simplethreadpool.h"
#include "simpleworkstealingpool.h"
#include "stdblockingqueue.h"


#define ASSERT_THAT(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            abort(); \
        } \
    } while (0)

void testFixedFunction()
{
    int src, dst;
    // Should have capture with sizeof == 2 * sizeof(int*).
    FixedFunction<void()> proc([&src, &dst] { dst = src; });
    ASSERT_THAT(!proc.empty());
    src = 1;
    proc();
    ASSERT_THAT(dst == 1);
    src = 123;
    proc();
    ASSERT_THAT(dst == 123);

    FixedFunction<void()> movedProc(std::move(proc));
    ASSERT_THAT(!movedProc.empty());
    ASSERT_THAT(proc.empty());
    src = 456;
    movedProc();
    ASSERT_THAT(dst == 456);

    FixedFunction<void()> moveAssignedProc;
    ASSERT_THAT(moveAssignedProc.empty());
    moveAssignedProc = std::move(movedProc);
    ASSERT_THAT(!moveAssignedProc.empty());
    ASSERT_THAT(movedProc.empty());
    src = 789;
    moveAssignedProc();
    ASSERT_THAT(dst == 789);

    double coeff = 1.0;
    FixedFunction<double(double)> computeFunc1([&coeff] (double x) { return sqrt(x) * coeff; });
    ASSERT_THAT(computeFunc1(1.0) == 1.0);
    ASSERT_THAT(computeFunc1(4.0) == 2.0);
    coeff = 3.0;
    ASSERT_THAT(computeFunc1(1.0) == 3.0);

    FixedFunction<double(double)> computeFunc2(sqrt);
    ASSERT_THAT(computeFunc2(1.0) == 1.0);
    ASSERT_THAT(computeFunc2(4.0) == 2.0);

    struct RatherBigStruct {
        double d1 = 1.0;
        double d2 = 2.0;
        double d3 = 3.0;
        double d4 = 4.0;
        double d5 = 5.0;
        double d6 = 6.0;
        double d7 = 7.0;
    };

    FixedFunction<double(double)> smallAndBigFunc1([](double param) { return param + 1.0; });
    ASSERT_THAT(smallAndBigFunc1(0.0) == 1.0);
    ASSERT_THAT(smallAndBigFunc1(1.0) == 2.0);

    RatherBigStruct rbs;
    FixedFunction<double(double)> smallAndBigFunc2([=](double param) {
        return rbs.d1 + rbs.d2 + rbs.d3 + rbs.d4 + rbs.d5 + rbs.d6 + rbs.d7 + param;
    });
    ASSERT_THAT(smallAndBigFunc2(0.0) == 28.0);

    FixedFunction<double(double)> smallAndBigFunc3;
    smallAndBigFunc3 = std::move(smallAndBigFunc2);
    smallAndBigFunc2 = std::move(smallAndBigFunc1);
    ASSERT_THAT(smallAndBigFunc2(0.0) == 1.0);
    ASSERT_THAT(smallAndBigFunc3(0.0) == 28.0);

    FixedFunction<std::string(std::string)> copyStringFunc([](std::string s) {
        return s + "abc";
    });
    ASSERT_THAT(copyStringFunc("123") == "123abc");

    FixedFunction<std::string(std::string const&)> crefStringFunc([](std::string const& s) {
        return s + "abcd";
    });

    ASSERT_THAT(crefStringFunc("1234") == "1234abcd");

    FixedFunction<void(std::string const&, std::string&)> refStringFunc(
                [](std::string const& srcs, std::string& dsts) {
        dsts = srcs + "abcde";
    });

    std::string out;
    refStringFunc("12345", out);
    ASSERT_THAT(out == "12345abcde");

    printf("FixedFunction tests passed\n");
    printf("=====\n");
}

void testCountWaiter()
{
    const int kNumIterations = 10000;
    const int kRandomSpinMax = 10000;

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
            ASSERT_THAT(i * i == producedValue[i]);
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
        // Note the count 2: both producer threads must finish before both consumer threads
        // continue.
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
    printf("=====\n");
}

// T must have a constructor T(int) and have a comparison operator==(T const&, T const&).
template<typename T, typename Queue>
void testQueueImpl(Queue& testQueue, const char* typeName, const char* testQueueName, int numIters,
                   int64_t const baselineSpeed[8], int64_t newSpeed[8])
{
    // Run several producer and one consumer thread.
    auto producer = [](auto& queue, int start, int end, int step) {
        uint64_t startTime = getTimeTicks();
        for (int i = start; i < end; i += step) {
            ASSERT_THAT(queue.enqueue(T(i)));
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
            ASSERT_THAT(out[i] == T(i));
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
        int64_t pushPerSec = numIters * 1000 / producerTime;
        printf("Pushed %lld/sec elements in %s<%s>", (long long)pushPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(0, pushPerSec);
        printf("\n");

        consumerThread.join();
        assertCorrectOut(testOut);
        int64_t popPerSec = numIters * 1000 / consumerTime;
        printf("Popped %lld/sec elements from %s<%s>", (long long)popPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(1, popPerSec);
        printf("\n");

        printf("-----\n");
    }

    // Run one producer and one consumer after 500msec.
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
        int64_t pushPerSec = numIters * 1000 / producerTime;
        printf("Pushed %lld/sec elements in %s<%s>", (long long)pushPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(2, pushPerSec);
        printf("\n");

        consumerThread.join();
        assertCorrectOut(testOut);
        int64_t popPerSec = numIters * 1000 / consumerTime;
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
        int64_t pushPerSec = numIters * 1000 / std::max(producerTime1, producerTime2);
        printf("Pushed %lld/sec elements from two threads in %s<%s>", (long long)pushPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(4, pushPerSec);
        printf("\n");

        consumerThread.join();
        assertCorrectOut(testOut);
        int64_t popPerSec = numIters * 1000 / consumerTime;
        printf("Popped %lld/sec elements from %s<%s>", (long long)popPerSec, testQueueName,
               typeName);
        computeAndPrintRatio(5, popPerSec);
        printf("\n");

        printf("-----\n");
    }

    // Run two producers and one consumer after 500msec.
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
        int64_t pushPerSec = numIters * 1000 / std::max(producerTime1, producerTime2);
        printf("Pushed %lld/sec elements from two threads in %s<%s>", (long long)pushPerSec,
               testQueueName, typeName);
        computeAndPrintRatio(6, pushPerSec);
        printf("\n");

        consumerThread.join();
        assertCorrectOut(testOut);
        int64_t popPerSec = numIters * 1000 / consumerTime;
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

        BlockingQueue<int, mpmc_bounded_queue> mpmcBlockingQueueInt(kQueueSize);
        testQueueImpl<int>(mpmcBlockingQueueInt, "int", "mpmc_bounded_queue", kIters,
                           baselineSpeedInt, nullptr);

        printf("=====\n");
    }

    {
        printf("Testing FatQueueItem queues\n");

        int64_t baselineSpeedFat[8];

        StdBlockingQueue<FatQueueItem> stdBlockingQueueFat;
        testQueueImpl<FatQueueItem>(stdBlockingQueueFat, "FatQueueItem", "StdBlockingQueue", kIters,
                                    nullptr, baselineSpeedFat);

        BlockingQueue<FatQueueItem, mpmc_bounded_queue> mpmcBlockingQueueFat(kQueueSize);
        testQueueImpl<FatQueueItem>(mpmcBlockingQueueFat, "FatQueueItem", "mpmc_bounded_queue",
                                    kIters, baselineSpeedFat, nullptr);

        printf("=====\n");
    }
}


struct FiberTestState
{
    int numIterations;
    double startOutput;
    double* output;
    FiberId nextFiber;
};

void fiberTestFunc(void* arg)
{
    FiberTestState* state = (FiberTestState*)arg;
    for (int i = 0; i < state->numIterations; i++) {
        state->output[i] = i * state->startOutput;
        state->nextFiber.switchTo();
    }
}

struct TrivialFiberTestState
{
    int numIterations;
    FiberId nextFiber;
};

void trivialFiberTestFunc(void* arg)
{
    TrivialFiberTestState* state = (TrivialFiberTestState*)arg;

    for (int i = 0; i < state->numIterations; i++) {
        state->nextFiber.switchTo();
    }
}

void testFiber()
{
    {
        // Create two groups of fibers on one thread, each of the groups circularly pointing
        // to the next in-group fiber. Start the first group, start the second group, see that
        // all fibers have completed.
        int const kNumIterations = 10000;
        // kNumFibers must be divisible by 2.
        int const kNumFibers = 8;
        std::unique_ptr<double[]> output{new double[kNumIterations * kNumFibers]};
        FiberTestState state[kNumFibers];
        FiberId fibers[kNumFibers];

        for (int i = 0; i < kNumFibers; i++) {
            state[i].numIterations = kNumIterations;
            state[i].startOutput = i;
            state[i].output = &output[i * kNumIterations];
            fibers[i] = FiberId::create(256 * 1024, fiberTestFunc, &state[i]);
        }
        for (int i = 0; i < kNumFibers; i++) {
            // Make two groups of fibers: 0-2-4-... and 1-3-5-...
            state[i].nextFiber = fibers[(i + 2) % kNumFibers];
        }

        // Run one group of fibers on another thread to test if anything breaks when sharing fibers
        // between the threads.
        std::thread fiberRunnerThread([&] {
            // Run the first group of fibers.
            fibers[0].switchTo();
        });
        // Run the second group of fibers.
        fibers[1].switchTo();
        fiberRunnerThread.join();

        for (int j = 0; j < kNumFibers; j++) {
            double startOutput = j;
            for (int i = 0; i < kNumIterations; i++) {
                ASSERT_THAT(output[j * kNumIterations + i] == startOutput * i);
            }
        }

        for (int i = 0; i < kNumFibers; i++) {
            fibers[i].destroy();
        }
    }

    int64_t switchesPerSecond;
    {
        // Run trivial fibers switching to next one in a loop.
        int const kNumIterations = 1000000;
        int const kNumFibers = 10;
        TrivialFiberTestState state[kNumFibers];
        FiberId fibers[kNumFibers];

        for (int i = 0; i < kNumFibers; i++) {
            state[i].numIterations = kNumIterations;
            fibers[i] = FiberId::create(256 * 1024, trivialFiberTestFunc, &state[i]);
        }
        for (int i = 0; i < kNumFibers; i++) {
            state[i].nextFiber = fibers[(i + 1) % kNumFibers];
        }

        int64_t startTime = getTimeTicks();
        fibers[0].switchTo();
        int64_t runTime = getTimeTicks() - startTime;
        switchesPerSecond = kNumIterations * kNumFibers * getTimeFreq() / runTime;

        for (int i = 0; i < kNumFibers; i++) {
            fibers[i].destroy();
        }
    }

    printf("Fiber tests passed. %lld fiber switches per second\n", (long long)switchesPerSecond);
    printf("=====\n");
}


// Simplest sanity checks for SimpleThreadPool.
template<typename TP>
void basicTests(TP& tp)
{
    auto future1 = submitFuture(tp, [] { return 1; });
    ASSERT_THAT(future1.get() == 1);

    auto lambda2 = [](int i) { return i * i; };
    std::vector<std::future<int>> futures2;
    for (int i = 0; i < 10000; i++) {
        futures2.push_back(submitFuture(tp, lambda2, i));
    }
    for (int i = 0; i < 10000; i++) {
        ASSERT_THAT(futures2[i].get() == i * i);
    }

    auto future3 = submitFuture(tp, (double(*)(double))sqrt, 1.0);
    ASSERT_THAT(future3.get() == 1.0);

    int result4 = 0;
    CountWaiter waiter4(1);
    tp.submit([&waiter4, &result4] {
        result4 = 123;
        waiter4.post();
    });
    waiter4.wait();
    ASSERT_THAT(result4 == 123);

    printf("Basic tests passed\n");
}

struct TinyJobInput {
    double start = 0.0;
    int iters = 0;
};

double tinyJob(TinyJobInput const& input)
{
    double ret = 0.0;
    for (int i = 0; i < input.iters; i++) {
        ret += double(i + 1) * input.start;
    }
    return ret;
}

void prepareTinyJobInput(size_t numJobsPerBatch, int numItersPerJob,
                         std::vector<TinyJobInput>* jobInput)
{
    jobInput->resize(numJobsPerBatch);
    for (size_t i = 0; i < numJobsPerBatch; i++) {
        TinyJobInput& in = (*jobInput)[i];
        // Make the start and the end of the input array have much higher iters count to simulate
        // unbalanced workload.
        if (i < numJobsPerBatch / 10 || i > numJobsPerBatch * 9 / 10) {
            in.iters = numItersPerJob * 20;
        } else {
            in.iters = numItersPerJob;
        }
        in.start = M_PI / (i + 1);
    }
}

void printTinyJobsStats(std::vector<int64_t>& jobsPerSec, int64_t baseJobsPerSec,
                        std::vector<double> const& results, std::vector<double> const& baseResults,
                        size_t numJobsPerBatch, int numItersPerJob, char const* description)
{
    std::sort(jobsPerSec.begin(), jobsPerSec.end());
    int64_t avgJobsPerSec = simpleAverage(jobsPerSec);
    int64_t medianJobsPerSec = jobsPerSec[jobsPerSec.size() / 2];
    int64_t maxJobsPerSec = jobsPerSec.back();
    double accel = avgJobsPerSec * 100.0  / baseJobsPerSec;
    for (size_t i = 0; i < numJobsPerBatch; i++) {
        ASSERT_THAT(baseResults[i] == results[i]);
    }
    printf("Tiny job test with %d-iter-job for pool (%s): avg %lld, median %lld,"
           " max %lld jobs per sec, perf vs single core: %.1f%%\n", numItersPerJob, description,
           (long long)avgJobsPerSec, (long long)medianJobsPerSec, (long long)maxJobsPerSec, accel);

}

template<typename F>
void repeatForSeconds(int seconds, F&& func)
{
    int64_t testStartTime = getTimeTicks();
    int64_t timeFreq = getTimeFreq();
    while (getTimeTicks() - testStartTime < timeFreq * seconds) {
        func();
    }
}

template<typename T>
void printWorkStealingStats(T&)
{
    // Do nothing for all pools but SimpleWorkStealingPool.
}

template<typename T>
void printWorkStealingStats(SimpleWorkStealingPool<T>& tp)
{
    printf("Work-stealing stats: %lld semaphore posts, %lld semaphore waits, %lld try steals,"
           " %lld steals\n",
           (long long)tp.getTotalSemaphorePosts(), (long long)tp.getTotalSemaphoreWaits(),
           (long long)tp.getTotalTrySteals(), (long long)tp.getTotalSteals());
    tp.clearStats();
}

template<typename TP>
void tinyJobsTest(TP& tp, int numItersPerJob)
{
    static const size_t kNumJobsPerBatch = 10000;
    static const size_t kNumJobsPerSubBatch = kNumJobsPerBatch / 10;
    static const size_t kSeconds = 3;

    uint64_t timeFreq = getTimeFreq();

    std::vector<TinyJobInput> jobInput;
    prepareTinyJobInput(kNumJobsPerBatch, numItersPerJob, &jobInput);

    std::vector<double> baseResults;
    baseResults.resize(kNumJobsPerBatch);
    // Compute the amount of time to process jobs without multithreading and verify the results.
    int64_t baseStartTime = getTimeTicks();
    const size_t kNumRepeats = 50;
    for (size_t j = 0; j < kNumRepeats; j++) {
        for (size_t i = 0; i < kNumJobsPerBatch; i++) {
            baseResults[i] = tinyJob(jobInput[i]);
        }
    }
    int64_t baseJobsPerSec = timeFreq * (kNumRepeats * kNumJobsPerBatch)
            / (getTimeTicks() - baseStartTime);

    // Actually do five tests:
    //  1. for submitFuture and std::based future,
    //  2. for submit and CountWaiter,
    //  3. for submitRange and CountWaiter
    //  4. for submit and CountWaiter and results with padding.
    //  5. for submitRange and CountWaiter and results with padding.

//    // Run the test for ~3 seconds.
//    {
//        std::vector<std::future<double>> futures;
//        std::vector<double> results;
//        futures.resize(kNumJobsPerBatch);
//        results.resize(kNumJobsPerBatch);
//        std::vector<int64_t> jobsPerSec;
//        repeatForSeconds(kSeconds, [&] {
//            int64_t batchStartTime = getTimeTicks();
//            // Run a batch of tiny jobs and wait for them to complete.
//            for (size_t i = 0; i < kNumJobsPerBatch; i++) {
//                futures[i] = submitFuture(tp, tinyJob, jobInput[i]);
//            }

//            for (size_t i = 0; i < kNumJobsPerBatch; i++) {
//                results[i] = futures[i].get();
//            }
//            jobsPerSec.push_back(timeFreq * kNumJobsPerBatch / (getTimeTicks() - batchStartTime));
//        });
//        printTinyJobsStats(jobsPerSec, baseJobsPerSec, results, baseResults, kNumJobsPerBatch,
//                           numItersPerJob, "submit std::future");
//#if defined(WORK_STEALING_STATS)
//        printWorkStealingStats(tp);
//#endif
//    }

    // Run the test for ~3 seconds.
    {
        std::vector<double> results;
        results.resize(kNumJobsPerBatch);
        std::vector<int64_t> jobsPerSec;
        repeatForSeconds(kSeconds, [&] {
            int64_t batchStartTime = getTimeTicks();
            CountWaiter countWaiter(kNumJobsPerBatch);
            // Run a batch of tiny jobs and wait for them to complete.
            for (size_t i = 0; i < kNumJobsPerBatch; i++) {
                tp.submit([i, v = jobInput[i], &results, &countWaiter] {
                    results[i] = tinyJob(v);
                    countWaiter.post();
                });
            }

            countWaiter.wait();
            jobsPerSec.push_back(timeFreq * kNumJobsPerBatch / (getTimeTicks() - batchStartTime));
        });
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, results, baseResults, kNumJobsPerBatch,
                           numItersPerJob, "submit CountWaiter");
#if defined(WORK_STEALING_STATS)
        printWorkStealingStats(tp);
#endif
    }

    // Run the test for ~3 seconds.
    {
        std::vector<double> results;
        results.resize(kNumJobsPerBatch);
        std::vector<int64_t> jobsPerSec;
        repeatForSeconds(kSeconds, [&] {
            int64_t batchStartTime = getTimeTicks();
            CountWaiter countWaiter(kNumJobsPerBatch);
            // Run a batch of tiny jobs and wait for them to complete.
            for (size_t i = 0; i < kNumJobsPerBatch; i += kNumJobsPerSubBatch) {
                size_t num = std::min(kNumJobsPerSubBatch, kNumJobsPerBatch - i);
                tp.submitRange([&jobInput, &results, &countWaiter] (size_t from, size_t to) {
                    for (size_t j = from; j < to; j++) {
                        results[j] = tinyJob(jobInput[j]);
                    }
                    countWaiter.post(to - from);
                }, i, i + num);
            }

            countWaiter.wait();
            jobsPerSec.push_back(timeFreq * kNumJobsPerBatch / (getTimeTicks() - batchStartTime));
        });
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, results, baseResults, kNumJobsPerBatch,
                           numItersPerJob, "submitRange CountWaiter");
#if defined(WORK_STEALING_STATS)
        printWorkStealingStats(tp);
#endif
    }

    // Run the test for ~3 seconds.
    {
        // Assuming 64-bit double and 64-byte cacheline.
        const size_t kResultsStride = 8;
        std::vector<double> results;
        results.resize(kNumJobsPerBatch * kResultsStride);
        std::vector<int64_t> jobsPerSec;
        repeatForSeconds(kSeconds, [&] {
            int64_t batchStartTime = getTimeTicks();
            CountWaiter countWaiter(kNumJobsPerBatch);
            // Run a batch of tiny jobs and wait for them to complete.
            for (size_t i = 0; i < kNumJobsPerBatch; i++) {
                tp.submit([i, v = jobInput[i], &results, &countWaiter] {
                    results[i * kResultsStride] = tinyJob(v);
                    countWaiter.post();
                });
            }

            countWaiter.wait();
            jobsPerSec.push_back(timeFreq * kNumJobsPerBatch / (getTimeTicks() - batchStartTime));
        });

        // Convert from strided back to the simple array.
        std::vector<double> finalResults;
        finalResults.resize(kNumJobsPerBatch);
        for (size_t i = 0; i < kNumJobsPerBatch; i++) {
            finalResults[i] = results[i * kResultsStride];
        }
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, finalResults, baseResults, kNumJobsPerBatch,
                           numItersPerJob, "submit CountWaiter with results padding");
#if defined(WORK_STEALING_STATS)
        printWorkStealingStats(tp);
#endif
    }

    // Run the test for ~3 seconds.
    {
        // Assuming 64-bit double and 64-byte cacheline.
        const size_t kResultsStride = 8;
        std::vector<double> results;
        results.resize(kNumJobsPerBatch * kResultsStride);
        std::vector<int64_t> jobsPerSec;
        repeatForSeconds(kSeconds, [&] {
            int64_t batchStartTime = getTimeTicks();
            CountWaiter countWaiter(kNumJobsPerBatch);
            // Run a batch of tiny jobs and wait for them to complete.
            for (size_t i = 0; i < kNumJobsPerBatch; i += kNumJobsPerSubBatch) {
                size_t num = std::min(kNumJobsPerSubBatch, kNumJobsPerBatch - i);
                tp.submitRange([&jobInput, &results, &countWaiter] (size_t from, size_t to) {
                    for (size_t j = from; j < to; j++) {
                        results[j * kResultsStride] = tinyJob(jobInput[j]);
                    }
                    countWaiter.post(to - from);
                }, i, i + num);
            }

            countWaiter.wait();
            jobsPerSec.push_back(timeFreq * kNumJobsPerBatch / (getTimeTicks() - batchStartTime));
        });

        // Convert from strided back to the simple array.
        std::vector<double> finalResults;
        finalResults.resize(kNumJobsPerBatch);
        for (size_t i = 0; i < kNumJobsPerBatch; i++) {
            finalResults[i] = results[i * kResultsStride];
        }
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, finalResults, baseResults, kNumJobsPerBatch,
                           numItersPerJob, "submitRange CountWaiter with results padding");
#if defined(WORK_STEALING_STATS)
        printWorkStealingStats(tp);
#endif
    }
}

using TaskType = FixedFunction<void()>;

// A wrapper for MpMcBlockingQueue, providing the queue size.
size_t const kMaxTasksInQueue = 32 * 1024;
struct MpMcFixedBlockingQueue : public BlockingQueue<TaskType, mpmc_bounded_queue>
{
    MpMcFixedBlockingQueue()
        : BlockingQueue(kMaxTasksInQueue)
    {
    }
};

void printUsage(const char* argv0)
{
    printf("Usage: %s [options] [pool names]\n"
           "Options:\n"
           "\t--num-threads NUM\t\tSet number of threads in a pool (number of cores by default)\n"
           "Pool names:\n"
           "\tsimple\n"
           "\tsimple-mpmc\n"
           "\twork-stealing\n"
           "\tfiber\n",
           argv0);
}

int main(int argc, char** argv)
{
    int numThreads = std::thread::hardware_concurrency();
    std::set<std::string> poolNames;

    testFixedFunction();
    testCountWaiter();
    testQueues();

    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--num-threads") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --num-threads\n");
                return 1;
            }
            numThreads = atoi(argv[i + 1]);
            if (numThreads <= 0) {
                printf("Positive number must be specified for --num-threaads\n");
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            printf("Unknown argument: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        } else {
            poolNames.insert(argv[i]);
            i++;
        }
    }

    if (poolNames.empty() || setContains(poolNames, "simple")) {
        SimpleThreadPool<TaskType, StdBlockingQueue<TaskType>> tp(numThreads);

        printf("Running simple pool with %d threads\n", tp.numThreads());

        basicTests(tp);

        tinyJobsTest(tp, 20);
        tinyJobsTest(tp, 200);
//        tinyJobsTest(tp, 2000);
    }

    if (poolNames.empty() || setContains(poolNames, "simple-mpmc")) {
        SimpleThreadPool<TaskType, MpMcFixedBlockingQueue> tp(numThreads);

        printf("Running simple mpmc pool with %d threads\n", tp.numThreads());

        basicTests(tp);

        tinyJobsTest(tp, 20);
        tinyJobsTest(tp, 200);
//        tinyJobsTest(tp, 2000);
    }

    if (poolNames.empty() || setContains(poolNames, "work-stealing")) {
        SimpleWorkStealingPool<TaskType> tp(numThreads);

        printf("Running work stealing pool with %d threads\n", tp.numThreads());

        basicTests(tp);
#if defined(WORK_STEALING_STATS)
        tp.clearStats();
#endif

        tinyJobsTest(tp, 20);
        tinyJobsTest(tp, 200);
//        tinyJobsTest(tp, 2000);
    }

    if (poolNames.empty() || setContains(poolNames, "fiber")) {
        testFiber();
    }
}
