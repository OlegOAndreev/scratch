#include "common.h"

#include <cmath>
#include <thread>
#include <vector>

#include "blockingqueue.h"
#include "countwaiter.h"
#include "fixedfunction.h"
#include "futureutils.h"
#include "simplethreadpool.h"
#include "simpleworkstealingpool.h"
#include "stdblockingqueue.h"

// Simplest sanity checks for SimpleThreadPool.
template<typename TP>
void basicTests(TP& tp)
{
    auto future1 = submitFuture(tp, [] { return 1; });
    ENSURE(future1.get() == 1, "");

    auto lambda2 = [](int i) { return i * i; };
    std::vector<std::future<int>> futures2;
    for (int i = 0; i < 10000; i++) {
        futures2.push_back(submitFuture(tp, lambda2, i));
    }
    for (int i = 0; i < 10000; i++) {
        ENSURE(futures2[i].get() == i * i, "");
    }

    auto future3 = submitFuture(tp, (double(*)(double))sqrt, 1.0);
    ENSURE(future3.get() == 1.0, "");

    int result4 = 0;
    CountWaiter waiter4(1);
    tp.submit([&waiter4, &result4] {
        result4 = 123;
        waiter4.post();
    });
    waiter4.wait();
    ENSURE(result4 == 123, "");

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
        ENSURE(baseResults[i] == results[i], "");
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
struct MpMcFixedBlockingQueue : public BlockingQueue<mpmc_bounded_queue<TaskType>>
{
    MpMcFixedBlockingQueue()
        : BlockingQueue(kMaxTasksInQueue)
    {
    }
};

void testPoolSimple(int numThreads)
{
    SimpleThreadPool<TaskType, StdBlockingQueue<TaskType>> tp(numThreads);

    printf("Running simple pool with %d threads\n", tp.numThreads());

    basicTests(tp);

    tinyJobsTest(tp, 20);
    tinyJobsTest(tp, 200);
//        tinyJobsTest(tp, 2000);
}

void testPoolSimpleMpMc(int numThreads)
{
    SimpleThreadPool<TaskType, MpMcFixedBlockingQueue> tp(numThreads);

    printf("Running simple mpmc pool with %d threads\n", tp.numThreads());

    basicTests(tp);

    tinyJobsTest(tp, 20);
    tinyJobsTest(tp, 200);
//        tinyJobsTest(tp, 2000);
}

void testPoolWorkStealing(int numThreads)
{
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
