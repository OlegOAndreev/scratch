#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "countwaiter.h"
#include "fixedfunction.h"
#include "mpmcblockingtaskqueue.h"
#include "simpleblockingtaskqueue.h"
#include "simplethreadpool.h"
#include "workstealingpool.h"


#define ASSERT_THAT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        } \
    } while (0)

void testFixedFunction()
{
    int src, dst;
    // Should have capture with sizeof == 2 * sizeof(int*).
    FixedFunction<void()> proc([&src, &dst] { dst = src; });
    src = 1;
    proc();
    ASSERT_THAT(dst == 1);
    src = 123;
    proc();
    ASSERT_THAT(dst == 123);

    FixedFunction<void()> movedProc(std::move(proc));
    src = 456;
    movedProc();
    ASSERT_THAT(dst == 456);

    double coeff = 1.0;
    FixedFunction<double(double)> computeFunc1([&coeff] (double x) { return sqrt(x) * coeff; });
    ASSERT_THAT(computeFunc1(1.0) == 1.0);
    ASSERT_THAT(computeFunc1(4.0) == 2.0);
    coeff = 3.0;
    ASSERT_THAT(computeFunc1(1.0) == 3.0);

    FixedFunction<double(double)> computeFunc2(sqrt);
    ASSERT_THAT(computeFunc2(1.0) == 1.0);
    ASSERT_THAT(computeFunc2(4.0) == 2.0);

    printf("FixedFunction tests passed\n");
}


using SimpleThreadPool = SimpleThreadPoolImpl<FixedFunction<void()>, SimpleBlockingTaskQueue>;
using MpMcThreadPool = SimpleThreadPoolImpl<FixedFunction<void()>, MpMcBlockingTaskQueue>;
using WorkStealingPool = WorkStealingPoolImpl<FixedFunction<void()>>;

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
        ret += sqrt(i * input.start);
    }
    return ret;
}

void prepareTinyJobInput(size_t numJobsPerBatch, int numItersPerJob, std::vector<TinyJobInput>* jobInput)
{
    jobInput->resize(numJobsPerBatch);
    for (size_t i = 0; i < numJobsPerBatch; i++) {
        TinyJobInput& in = (*jobInput)[i];
        in.iters = numItersPerJob;
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
           " max %lld jobs per sec, accel vs single core: %.1f%%\n", numItersPerJob, description,
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
    const size_t kNumRepeats = 10;
    for (size_t j = 0; j < kNumRepeats; j++) {
        for (size_t i = 0; i < kNumJobsPerBatch; i++) {
            baseResults[i] = tinyJob(jobInput[i]);
        }
    }
    int64_t baseJobsPerSec = timeFreq * (kNumRepeats * kNumJobsPerBatch) / (getTimeTicks() - baseStartTime);

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
//        printTinyJobsStats(jobsPerSec, baseJobsPerSec, results, baseResults, kNumJobsPerBatch, numItersPerJob,
//                           "submit std::future");
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
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, results, baseResults, kNumJobsPerBatch, numItersPerJob,
                           "submit CountWaiter");
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
                tp.submitRange([&jobInput, &results, &countWaiter] (size_t base, size_t n) {
                    for (size_t j = base; j < base + n; j++) {
                        results[j] = tinyJob(jobInput[j]);
                    }
                    countWaiter.post(n);
                }, i, num);
            }

            countWaiter.wait();
            jobsPerSec.push_back(timeFreq * kNumJobsPerBatch / (getTimeTicks() - batchStartTime));
        });
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, results, baseResults, kNumJobsPerBatch, numItersPerJob,
                           "submitRange CountWaiter");
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
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, finalResults, baseResults, kNumJobsPerBatch, numItersPerJob,
                           "submit CountWaiter with results padding");
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
                tp.submitRange([&jobInput, &results, &countWaiter] (size_t base, size_t n) {
                    for (size_t j = base; j < base + n; j++) {
                        results[j * kResultsStride] = tinyJob(jobInput[j]);
                    }
                    countWaiter.post(n);
                }, i, num);
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
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, finalResults, baseResults, kNumJobsPerBatch, numItersPerJob,
                           "submitRange CountWaiter with results padding");
    }
}

void printUsage(const char* argv0)
{
    printf("Usage: %s [options] [pool names]\n"
           "Options:\n"
           "\t--num-threads NUM\t\tSet number of threads in a pool (number of cores by default)\n"
           "Pool names:\n"
           "\tsimple\n"
           "\tsimple-mpmc\n",
           argv0);
}

int main(int argc, char** argv)
{
    int numThreads = std::thread::hardware_concurrency();
    std::set<std::string> poolNames;

    testFixedFunction();

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
        SimpleThreadPool tp(numThreads);

        printf("Running simple pool with %d threads\n", tp.numThreads());

        basicTests(tp);

        tinyJobsTest(tp, 1);
        tinyJobsTest(tp, 20);
        tinyJobsTest(tp, 200);
        tinyJobsTest(tp, 2000);
    }

    if (poolNames.empty() || setContains(poolNames, "simple-mpmc")) {
        MpMcThreadPool tp(numThreads);

        printf("Running simple mpmc pool with %d threads\n", tp.numThreads());

        basicTests(tp);

        tinyJobsTest(tp, 1);
        tinyJobsTest(tp, 20);
//        tinyJobsTest(tp, 200);
//        tinyJobsTest(tp, 2000);
    }

    if (poolNames.empty() || setContains(poolNames, "work-stealing")) {
        WorkStealingPool tp(numThreads);

        printf("Running work stealing pool with %d threads\n", tp.numThreads());

        basicTests(tp);

        tinyJobsTest(tp, 1);
#if defined(WORK_STEALING_STATS)
        printf("Work-stealing stats: %lld semaphore posts, %lld semaphore waits, %lld try steals, %lld steals\n",
               (long long)tp.totalSemaphorePosts.load(), (long long)tp.totalSemaphoreWaits.load(),
               (long long)tp.totalTrySteals.load(), (long long)tp.totalSteals.load());
#endif
        tinyJobsTest(tp, 20);
#if defined(WORK_STEALING_STATS)
        printf("Work-stealing stats: %lld semaphore posts, %lld semaphore waits, %lld try steals, %lld steals\n",
               (long long)tp.totalSemaphorePosts.load(), (long long)tp.totalSemaphoreWaits.load(),
               (long long)tp.totalTrySteals.load(), (long long)tp.totalSteals.load());
#endif
//        tinyJobsTest(tp, 200);
//        tinyJobsTest(tp, 2000);
    }
}
