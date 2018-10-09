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
#include "simplethreadpool.h"
#include "mpmc_bounded_queue/mpmc_bounded_queue.h"


// The simple task queue, protected by the lock + condvar.
template<typename Task>
class SimpleTaskQueue {
public:
    void push(Task&& task)
    {
        std::unique_lock<std::mutex> l(lock);
        tasks.push_back(std::move(task));
        workerWakeup.notify_one();
    }

    bool pop(Task& task)
    {
        std::unique_lock<std::mutex> l(lock);
        if (stopFlag) {
            return false;
        }
        workerWakeup.wait(l, [this] { return stopFlag || !tasks.empty(); });
        if (stopFlag) {
            return false;
        }
        task = std::move(tasks.front());
        tasks.pop_front();
        return true;
    }

    void stop()
    {
        std::unique_lock<std::mutex> l(lock);
        stopFlag = true;
        workerWakeup.notify_all();
    }

private:
    std::deque<Task> tasks;
    bool stopFlag;

    std::mutex lock;
    std::condition_variable workerWakeup;
};

// Task queue based on mpmc_bounded_queue + semaphore for sleeping when the tasks are absent.
template<typename Task>
class MpMcTaskQueue {
public:
    MpMcTaskQueue()
        : workerQueue(kMaxTasksInQueue)
    {
    }

    void push(Task&& task)
    {
        if (!workerQueue.enqueue(std::move(task))) {
            // TODO: This is not a production-ready solution.
            exit(1);
        }
        // NOTE: There is a non-obvious potential race condition here: if the queue is empty and thread 1 (worker)
        // is trying to sleep after checking that it is empty and thread 2 is trying to add the new task,
        // the following can (potentially) happen:
        //  Thread 1:
        //   1. checks that queue is empty (passes)
        //   2. increments numSleepingWorkers (0 -> 1)
        //   3. checks that queue is empty
        //  Thread 2:
        //   1. adds new item to the queue (queue becomes non-empty)
        //   2. reads numSleepingWorkers
        //
        // Originally I tried solving this problem by using acq_rel/relaxed when writing/reading numSleepingWorkers
        // and acq_rel (via atomic::exchange RMW) when updating cell.sequence_ in mpmc_bounded_queue::dequeue. This
        // has been based on the reasoning that acquire and release match the LoadLoad+LoadStore and
        // LoadStore+StoreStore barrier correspondingly and acq_rel RMW is, therefore, a total barrier. However, that is
        // not what part 1.10 of the C++ standard says: discussed in
        // https://stackoverflow.com/questions/52606524/what-exact-rules-in-the-c-memory-model-prevent-reordering-before-acquire-opera/
        // The easiest fix is simpley changing all the related accesses to seq_cst:
        //  * all reads and writes on numSleepingWorkers
        //  * first read in dequeue and last write in enqueue.
        // The good thing is that the generated code for acq_rel RMW is identical to seq_cst store on the relevant
        // platforms (x86-64 and aarch64).
        if (numSleepingWorkers.load(std::memory_order_seq_cst) > 0) {
            sleepingSemaphore.post();
        }
    }

    bool pop(Task& task)
    {
        while (true) {
            if (stopFlag.load(std::memory_order_relaxed)) {
                return false;
            }
            // Spin for a few iterations
            for (int i = 0; i < kSpinCount; i++) {
                if (workerQueue.dequeue(task)) {
                    return true;
                }
            }

            // Sleep until the new task arrives.
            numSleepingWorkers.fetch_add(1, std::memory_order_seq_cst);

            // Recheck that there are still no tasks in the queue. This is used to prevent the race condition,
            // where the pauses between checking the queue first and incrementing the numSleepingWorkers, while the task
            // is submitted during this pause.
            // NOTE: See NOTE in the submitImpl for the details on correctness of the sleep.
            if (workerQueue.dequeue(task)) {
                numSleepingWorkers.fetch_sub(1, std::memory_order_seq_cst);
                task();
            } else {
                sleepingSemaphore.wait();
                numSleepingWorkers.fetch_sub(1, std::memory_order_seq_cst);
            }
        }
    }

    void stop()
    {
        stopFlag.store(true);
        sleepingSemaphore.post();
    }

private:
    size_t const kMaxTasksInQueue = 256 * 1024;
    int const kSpinCount = 1000;

    mpmc_bounded_queue<Task> workerQueue;
    std::atomic<int> numSleepingWorkers{0};
    Semaphore sleepingSemaphore;
    std::atomic<bool> stopFlag{false};
};

using SimpleThreadPool = SimpleThreadPoolImpl<std::packaged_task<void()>, SimpleTaskQueue>;
using MpMcThreadPool = SimpleThreadPoolImpl<std::packaged_task<void()>, MpMcTaskQueue>;

#define ASSERT_THAT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        } \
    } while (0)

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
        ret += cos(i * input.start);
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

template<typename TP>
void tinyJobsTest(TP& tp, int numItersPerJob)
{
    static const size_t kNumJobsPerBatch = 10000;
    int64_t timeFreq = getTimeFreq();

    std::vector<TinyJobInput> jobInput;
    prepareTinyJobInput(kNumJobsPerBatch, numItersPerJob, &jobInput);

    std::vector<double> baseResults;
    baseResults.resize(kNumJobsPerBatch);
    // Compute the amount of time to process jobs without multithreading and verify the results.
    int64_t baseStartTime = getTimeCounter();
    size_t const kNumRepeats = 10;
    for (size_t j = 0; j < kNumRepeats; j++) {
        for (size_t i = 0; i < kNumJobsPerBatch; i++) {
            baseResults[i] = tinyJob(jobInput[i]);
        }
    }
    int64_t baseJobsPerSec = timeFreq * (kNumRepeats * kNumJobsPerBatch) / (getTimeCounter() - baseStartTime);

    // Actually do three tests:
    //  1. for submitFuture and std::based future,
    //  2. for simple submit and CountWaiter,
    //  3. for simple submit results with padding.

    // Run the first test for ~3 seconds.
    {
        std::vector<std::future<double>> futures;
        std::vector<double> results;
        futures.resize(kNumJobsPerBatch);
        results.resize(kNumJobsPerBatch);
        int64_t testStartTime = getTimeCounter();
        std::vector<int64_t> jobsPerSec;
        while (getTimeCounter() - testStartTime < timeFreq * 3) {
            int64_t batchStartTime = getTimeCounter();
            // Run a batch of tiny jobs and wait for them to complete.
            for (size_t i = 0; i < kNumJobsPerBatch; i++) {
                futures[i] = submitFuture(tp, tinyJob, jobInput[i]);
            }

            for (size_t i = 0; i < kNumJobsPerBatch; i++) {
                results[i] = futures[i].get();
            }
            jobsPerSec.push_back(timeFreq * kNumJobsPerBatch / (getTimeCounter() - batchStartTime));
        }
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, results, baseResults, kNumJobsPerBatch, numItersPerJob,
                           "with std::future");
    }

    // Run the second test for ~3 seconds.
    {
        int64_t testStartTime = getTimeCounter();
        std::vector<double> results;
        results.resize(kNumJobsPerBatch);
        std::vector<int64_t> jobsPerSec;
        while (getTimeCounter() - testStartTime < timeFreq * 3) {
            int64_t batchStartTime = getTimeCounter();
            CountWaiter countWaiter(kNumJobsPerBatch);
            // Run a batch of tiny jobs and wait for them to complete.
            for (size_t i = 0; i < kNumJobsPerBatch; i++) {
                tp.submit([i, &results, &jobInput, &countWaiter] {
                    results[i] = tinyJob(jobInput[i]);
                    countWaiter.post();
                });
            }

            countWaiter.wait();
            jobsPerSec.push_back(timeFreq * kNumJobsPerBatch / (getTimeCounter() - batchStartTime));
        }
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, results, baseResults, kNumJobsPerBatch, numItersPerJob,
                           "with CountWaiter");
    }

    // Run the second test for ~3 seconds.
    {
        // Assuming 64-bit double and 64-byte cacheline.
        size_t const kResultsStride = 8;
        int64_t testStartTime = getTimeCounter();
        std::vector<double> results;
        results.resize(kNumJobsPerBatch * kResultsStride);
        std::vector<int64_t> jobsPerSec;
        while (getTimeCounter() - testStartTime < timeFreq * 3) {
            int64_t batchStartTime = getTimeCounter();
            CountWaiter countWaiter(kNumJobsPerBatch);
            // Run a batch of tiny jobs and wait for them to complete.
            for (size_t i = 0; i < kNumJobsPerBatch; i++) {
                tp.submit([i, &results, &jobInput, &countWaiter] {
                    results[i * kResultsStride] = tinyJob(jobInput[i]);
                    countWaiter.post();
                });
            }

            countWaiter.wait();
            jobsPerSec.push_back(timeFreq * kNumJobsPerBatch / (getTimeCounter() - batchStartTime));
        }

        // Convert from strided back to the simple array.
        std::vector<double> finalResults;
        finalResults.resize(kNumJobsPerBatch);
        for (size_t i = 0; i < kNumJobsPerBatch; i++) {
            finalResults[i] = results[i * kResultsStride];
        }
        printTinyJobsStats(jobsPerSec, baseJobsPerSec, finalResults, baseResults, kNumJobsPerBatch, numItersPerJob,
                           "with results padding");
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
        tinyJobsTest(tp, 200);
        tinyJobsTest(tp, 2000);
    }

//    if (poolNames.empty() || setContains(poolNames, "better-custom")) {
//        BetterThreadPoolCustom btp(numThreads);

//        printf("Running better pool (std::promise, BetterTask) with %d threads\n", btp.numThreads());

//        basicTests(btp);

//        tinyJobsTest(&btp, 1);
//        tinyJobsTest(&btp, 20);
//        tinyJobsTest(&btp, 200);
//        tinyJobsTest(&btp, 2000);
//    }
}
