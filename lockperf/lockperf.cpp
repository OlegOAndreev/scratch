#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

const size_t CACHE_LINE_WIDTH = 64;

#if defined(__linux__)

#include <semaphore.h>

struct Semaphore
{
    sem_t sema;

    Semaphore()
    {
        sem_init(&sema, 0, 0);
    }

    void post()
    {
        sem_post(&sema);
    }

    void wait()
    {
        sem_wait(&sema);
    }
};

#elif defined(__APPLE__)

#include <dispatch/dispatch.h>

struct Semaphore
{
    dispatch_semaphore_t sema;

    Semaphore()
    {
        sema = dispatch_semaphore_create(0);
    }

    void post()
    {
        dispatch_semaphore_signal(sema);
    }

    void wait()
    {
        dispatch_semaphore_wait(sema, ~uint64_t(0));
    }
};

#else

#error Specify a Semaphore implementation

#endif

struct MutexLock
{
    std::mutex mtx;

    int lock()
    {
        mtx.lock();
        return 0;
    }

    void unlock()
    {
        mtx.unlock();
    }
};

// The idea taken from http://preshing.com/20150316/semaphores-are-surprisingly-versatile/
struct SemaphoreLock {
    std::atomic<size_t> count;
    Semaphore semaphore;

    SemaphoreLock()
        : count(0)
    {
    }

    int lock()
    {
        for (int spins = 0; spins < 1000; spins++) {
            size_t expected = 0;
            if (count.compare_exchange_weak(expected, 1, std::memory_order_acq_rel)) {
                return spins;
            }
        }
        if (count.fetch_add(1, std::memory_order_acq_rel) > 0) {
            semaphore.wait();
        }
        return 1000;
    }

    void unlock()
    {
        if (count.fetch_sub(1, std::memory_order_release) > 1) {
            semaphore.post();
        }
    }
};

struct PauseBackoff
{
    static void backoff()
    {
        __asm __volatile("pause");
    }
};

struct NopBackoff
{
    static void backoff()
    {
        __asm __volatile("nop");
    }
};

struct SchedBackoff
{
    static void backoff()
    {
        sched_yield();
    }
};

struct SleepBackoff
{
    static void backoff()
    {
        usleep(0);
    }
};

struct EmptyBackoff
{
    static void backoff()
    {
    }
};

template <typename Backoff, size_t alignment, bool withLoad>
struct SpinLock
{
    std::atomic<size_t> flag;
    char padding[alignment - sizeof(size_t)];

    SpinLock()
        : flag(0)
    {
    }

    int lock()
    {
        int retryCount = 0;
        while (true) {
            size_t expected = 0;
            if (flag.compare_exchange_weak(expected, 1, std::memory_order_acq_rel)) {
                break;
            }
            if (withLoad) {
                while (flag.load(std::memory_order_acquire) != 0) {
                    Backoff::backoff();
                    retryCount++;
                }
            } else {
                Backoff::backoff();
                retryCount++;
            }
        }
        return retryCount;
    }

    void unlock()
    {
        flag.store(0, std::memory_order_release);
    }
};

template <typename Backoff, size_t alignment>
struct TicketLock
{
    std::atomic<size_t> in;
    char padding[alignment - sizeof(size_t)];
    std::atomic<size_t> out;

    TicketLock()
        : in(0)
        , out(0)
    {
    }

    int lock()
    {
        int retryCount = 0;
        size_t ticket = in.fetch_add(1, std::memory_order_acquire);
        while (out.load(std::memory_order_acquire) != ticket) {
            Backoff::backoff();
            retryCount++;
        }
        return retryCount;
    }

    void unlock()
    {
        out.fetch_add(1, std::memory_order_release);
    }
};

struct UnlockedWorkData
{
    std::vector<unsigned> input;
    SpinLock<EmptyBackoff, CACHE_LINE_WIDTH, true> removeMe;
    std::vector<unsigned> output;

    UnlockedWorkData(const std::vector<unsigned>& sourceInput)
        : input(sourceInput)
    {
        output.reserve(input.size());
    }

    bool popInput(unsigned& param, size_t& index, long long& /*retryCount*/)
    {
        if (input.empty()) {
            return false;
        }
        param = input.back();
        input.pop_back();
        index = input.size();
        return true;
    }

    void pushOutput(unsigned* values, size_t numValues, long long& /*retryCount*/)
    {
        // wtf???? adding lock here improves the perf. is this another one of clang's inline fuckups?
        output.insert(output.end(), values, values + numValues);
    }
};

template <typename Lock>
struct LockingWorkData
{
    Lock inputLock;
    std::vector<unsigned> input;

    Lock outputLock;
    std::vector<unsigned> output;

    LockingWorkData(const std::vector<unsigned>& sourceInput)
        : input(sourceInput)
    {
        output.reserve(input.size());
    }

    bool popInput(unsigned& param, size_t& index, long long& retryCount)
    {
        retryCount += inputLock.lock();
        if (input.empty()) {
            inputLock.unlock();
            return false;
        }
        param = input.back();
        input.pop_back();
        inputLock.unlock();
        index = input.size();
        return true;
    }

    void pushOutput(unsigned* values, size_t numValues, long long& retryCount)
    {
        retryCount += outputLock.lock();
        output.insert(output.end(), values, values + numValues);
        outputLock.unlock();
    }
};

struct LockFreeWorkData
{
    std::atomic<size_t> inputIndex;
    char padding1[CACHE_LINE_WIDTH - sizeof(size_t)];
    std::vector<unsigned> input;
    char padding2[CACHE_LINE_WIDTH - sizeof(size_t)];

    std::atomic<size_t> outputIndex;
    char padding3[CACHE_LINE_WIDTH - sizeof(size_t)];
    std::vector<unsigned> output;
    char padding4[CACHE_LINE_WIDTH - sizeof(size_t)];

    LockFreeWorkData(const std::vector<unsigned>& sourceInput)
        : inputIndex(sourceInput.size())
        , input(sourceInput)
        , outputIndex(0)
    {
        output.resize(input.size());
    }

    bool popInput(unsigned& param, size_t& index, long long& /*retryCount*/)
    {
        size_t curIndex = inputIndex.fetch_sub(1, std::memory_order_relaxed);
        // The index can become underflow. This loop is generally a bad code, but we want to simulate
        // the popping from the back of input vector that LockingWorkData does.
        if (curIndex > 0 && curIndex <= input.size()) {
            index = curIndex - 1;
            param = input[curIndex - 1];
            return true;
        } else {
            return false;
        }
    }

    void pushOutput(unsigned* values, size_t numValues, long long& /*retryCount*/)
    {
        size_t curIndex = outputIndex.fetch_add(numValues, std::memory_order_relaxed);
        memmove(output.data() + curIndex, values, numValues * sizeof(*values));
    }
};

// Use unsigned for work, so that we do not get UB on overflows.
void generateWork(int inputSize, int workAmount, std::vector<unsigned>& input)
{
    int minWork = (workAmount > 1) ? workAmount - 1 : 1;
    int maxWork = workAmount + 1;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> uniform(minWork, maxWork);

    for (int i = 0; i < inputSize; i++) {
        unsigned r = uniform(gen);
        input.push_back(r * r * r);
    }
}

unsigned doWork(unsigned param)
{
    unsigned result = 0;
    for (unsigned i = 0; i < param; i++) {
        result += ~(i << 15);
        result ^= (i >> 10);
        result += (i << 3);
        result ^= (i >> 6);
        result += ~(i << 11);
        result ^= (i >> 16);
    }
    return result;
}

struct PerThreadStats
{
    long long retryCount;
    float avgRunLength;

    PerThreadStats()
        : retryCount(0)
        , avgRunLength(0.0)
    {
    }
};

template <typename WorkData>
void workerThread(WorkData& data, PerThreadStats& stats)
{
    std::vector<unsigned> output;
    output.reserve(data.input.size());

    int numRuns = 0;
    int totalRunLength = 0;
    int curRunLength = 0;
    size_t lastIndex = 0;

    unsigned param;
    size_t curIndex;
    long long retryCount = 0;
    while (data.popInput(param, curIndex, retryCount)) {
        output.push_back(doWork(param));

        if (curIndex != lastIndex - 1) {
            totalRunLength += curRunLength;
            curRunLength = 1;
            numRuns++;
        } else {
            curRunLength++;
        }
        lastIndex = curIndex;
    }

    totalRunLength += curRunLength;

    data.pushOutput(output.data(), output.size(), retryCount);
    
    if (numRuns > 0) {
        stats.avgRunLength = float(totalRunLength) / numRuns;
    }
    stats.retryCount = retryCount;
}

struct PerRunStats
{
    int timeMs;
    // The average run lengths of all the threads.
    float avgRunLength;
    // The total retry count of all the threads.
    long long totalRetryCount;

    PerRunStats()
        : timeMs(0)
        , avgRunLength(0.0)
        , totalRetryCount(0)
    {
    }
};

template <typename WorkData>
void runIteration(int numThreads, const std::vector<unsigned>& input, unsigned targetValue, PerRunStats& stats)
{
    std::vector<std::thread> threads;
    std::vector<PerThreadStats> threadStats;
    threadStats.resize(numThreads);

    WorkData data(input);
    Semaphore startSemaphore;
    for (int i = 0; i < numThreads; i++) {
        PerThreadStats& stat = threadStats[i];
        threads.emplace_back([&]() mutable {
            startSemaphore.wait();
            workerThread(data, stat);
        });
    }

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    for (int i = 0; i < numThreads; i++) {
        startSemaphore.post();
    }
    for (std::thread& thread: threads) {
        thread.join();
    }

    stats.timeMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();

    for (int i = 0; i < numThreads; i++) {
        stats.avgRunLength += threadStats[i].avgRunLength;
        stats.totalRetryCount += threadStats[i].retryCount;
    }
    stats.avgRunLength /= numThreads;

    // Check that we've successfully completed all work items.
    unsigned outputValue = std::accumulate(data.output.begin(), data.output.end(), 0);
    if (outputValue != targetValue) {
        printf("ERROR: Target value %u, output value %u, exiting.\n", targetValue, outputValue);
        exit(1);
    }
}

template <typename WorkData>
void run(const char* lockName, int numThreads, int inputSize, int workAmount)
{
    std::vector<unsigned> input;
    generateWork(inputSize, workAmount, input);
    unsigned targetValue = 0;
    for (int param : input) {
        targetValue += doWork(param);
    }

    int numTimes = 50;
    // The vector contains stats for each run.
    std::vector<PerRunStats> runStats;
    runStats.resize(numTimes);

    for (int i = 0; i < numTimes; i++) {
        runIteration<WorkData>(numThreads, input, targetValue, runStats[i]);
    }

    std::sort(runStats.begin(), runStats.end(), [](const PerRunStats& st1, const PerRunStats& st2) {
        return st1.timeMs < st2.timeMs;
    });
    const PerRunStats& minStat = runStats.front();
    const PerRunStats& p50Stat = runStats[runStats.size() / 2];
    const PerRunStats& p90Stat = runStats[runStats.size() * 9 / 10];
    const PerRunStats& p98Stat = runStats[runStats.size() * 49 / 50];
    printf("%s: Elapsed: %d-%d-%d-%dms (average sequential run length: %.2f-%.2f-%.2f-%.2f,"
            " retry count: %lld-%lld-%lld-%lld)\n",
            lockName, minStat.timeMs, p50Stat.timeMs, p90Stat.timeMs, p98Stat.timeMs,
            minStat.avgRunLength, p50Stat.avgRunLength, p90Stat.avgRunLength, p98Stat.avgRunLength,
            minStat.totalRetryCount, p50Stat.totalRetryCount, p90Stat.totalRetryCount, p98Stat.totalRetryCount);
}

int main(int argc, char** argv)
{
    // Descriptions of args:
    //  * numThreads: self-descriptive;
    //  * inputSize: all threads get trivial pieces of work from preallocated array, inputSize is the length of
    //    that array;
    //  * workAmount: the average length of work simulation loop in doWork will be workAmount ^ 3
    //    (use 1 for trivial one-iteration loops, use 5 for 125 iteration loops, 10 for 1000 loops).
    if (argc != 4) {
        printf("Usage: %s numThreads inputSize workAmount\n", argv[0]);
        return 1;
    }
    int numThreads = atoi(argv[1]);
    int inputSize = atoi(argv[2]);
    int workAmount = atoi(argv[3]);

    printf("Num threads: %d, input %d-%d size %d\n", numThreads, workAmount - 1, workAmount + 1, inputSize);

    if (numThreads == 1) {
        run<UnlockedWorkData>("no lock", numThreads, inputSize, workAmount);
    }
    run<LockFreeWorkData>("lock-free", numThreads, inputSize, workAmount);
    run<LockingWorkData<SpinLock<EmptyBackoff, CACHE_LINE_WIDTH, true>>>("spinlock", numThreads, inputSize, workAmount);
//     run<LockingWorkData<SpinLock<PauseBackoff, CACHE_LINE_WIDTH, true>>>("spinlock+pause", numThreads, inputSize, workAmount);
//     run<LockingWorkData<SpinLock<NopBackoff, CACHE_LINE_WIDTH, true>>>("spinlock+nop", numThreads, inputSize, workAmount);
//     run<LockingWorkData<SpinLock<SchedBackoff, CACHE_LINE_WIDTH, true>>>("spinlock+yield", numThreads, inputSize, workAmount);
//     run<LockingWorkData<SpinLock<SleepBackoff, CACHE_LINE_WIDTH, true>>>("spinlock+sleep", numThreads, inputSize, workAmount);
//     run<LockingWorkData<SpinLock<EmptyBackoff, CACHE_LINE_WIDTH, false>>>("spinlock,no load loop", numThreads, inputSize, workAmount);
//     run<LockingWorkData<SpinLock<PauseBackoff, CACHE_LINE_WIDTH, false>>>("spinlock+pause,no load loop", numThreads, inputSize, workAmount);
//     run<LockingWorkData<SpinLock<EmptyBackoff, sizeof(size_t), true>>>("spinlock,unaligned", numThreads, inputSize, workAmount);
//     run<LockingWorkData<SpinLock<PauseBackoff, sizeof(size_t), true>>>("spinlock+pause,unaligned", numThreads, inputSize, workAmount);
//     run<LockingWorkData<TicketLock<EmptyBackoff, CACHE_LINE_WIDTH>>>("ticketlock", numThreads, inputSize, workAmount);
//     run<LockingWorkData<TicketLock<EmptyBackoff, sizeof(size_t)>>>("ticketlock,unaligned", numThreads, inputSize, workAmount);
// #if !defined(__APPLE__)
//     run<LockingWorkData<MutexLock>>("std::mutex", numThreads, inputSize, workAmount);
// #endif
//     run<LockingWorkData<SemaphoreLock>>("semaphore", numThreads, inputSize, workAmount);

    return 0;
}
