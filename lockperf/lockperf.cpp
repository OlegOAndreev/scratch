#include <algorithm>
#include <atomic>
#include <mutex>
#include <random>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <sys/time.h>
#include <unistd.h>
#elif defined(__linux__)
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <process.h>
#include <windows.h>
#else
#error "Unsupported OS"
#endif

const size_t CACHE_LINE_WIDTH = 64;

int64_t getTimeCounter()
{
#if defined(__APPLE__)
    timeval tp;
    gettimeofday(&tp, nullptr);
    return tp.tv_sec * 1000000ULL + tp.tv_usec;
#elif defined(__linux__)
    timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
#elif defined(_WIN32)
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
#else
#error "Unsupported OS"
#endif
}

int64_t getTimeFreq()
{
#if defined(__APPLE__)
    return 1000000;
#elif defined(__linux__)
    return 1000000000;
#elif defined(_WIN32)
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    return li.QuadPart;
#else
#error "Unsupported OS"
#endif
}

#if defined(__linux__)
struct Semaphore
{
    sem_t sema;
    Semaphore() { sem_init(&sema, 0, 0); }
    ~Semaphore() { sem_destroy(&sema); }
    void post() { sem_post(&sema); }
    void wait() { sem_wait(&sema); }
};
#elif defined(__APPLE__)
struct Semaphore
{
    dispatch_semaphore_t sema;
    Semaphore() { sema = dispatch_semaphore_create(0); }
    ~Semaphore() { dispatch_release(sema); }
    void post() { dispatch_semaphore_signal(sema); }
    void wait() { dispatch_semaphore_wait(sema, ~uint64_t(0)); }
};
#elif defined(_WIN32)
struct Semaphore
{
    HANDLE sema;
    Semaphore() { sema = CreateSemaphore(NULL, 0, MAXLONG, NULL); }
    ~Semaphore() { CloseHandle(sema); }
    void post() { ReleaseSemaphore(sema, 1, NULL); }
    void wait() { WaitForSingleObject(sema, INFINITE); }
};
#else
#error "Unsupported OS"
#endif

#if defined(_WIN32) && defined(__GNUC__)
// MinGW ships without std::thread and std::mutex support, add a partial implementation of those.
namespace std {
    class mutex
    {
    public:
        mutex() { InitializeCriticalSection(&cs); }
        ~mutex() { DeleteCriticalSection(&cs); }
        void lock() { EnterCriticalSection(&cs); }
        void unlock() { LeaveCriticalSection(&cs); }
        
    private:
        CRITICAL_SECTION cs;
    };
    
    class thread
    {
    public:
        thread() : handle(0) {}
        thread(const thread& other) = delete;
        thread(thread&& other) : handle(other.handle) { other.handle = 0; }
        template<class Function, class... Args>
        explicit thread(Function&& f, Args&&... args) { handle = (HANDLE)_beginthreadex(NULL, 0, threadFunc, new ThreadFunction(f, args...), 0, NULL); }
        ~thread() { if (handle != 0) abort(); }
        thread& operator=(const thread&) = delete;
        thread& operator=(thread&& other) { if (handle != 0) abort(); handle = other.handle; other.handle = 0; }
        void join() { WaitForSingleObject(handle, INFINITE); CloseHandle(handle); handle = 0; }
    private:
        HANDLE handle;
        using ThreadFunction = std::function<void(void)>;
        static unsigned __stdcall threadFunc(void* arg)
        {
            std::unique_ptr<ThreadFunction> func((ThreadFunction*)arg);
            (*func)();
            return 0;
        }
    };
} // namespace std
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
struct SemaphoreLock
{
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

#if !defined(_MSC_VER)
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
#endif

struct SchedBackoff
{
    static void backoff()
    {
#if defined(__APPLE__) || defined(__linux__)
        sched_yield();
#elif defined(_WIN32)
        SwitchToThread();
#else
#error "Unsupported OS"
#endif
    }
};

struct SleepBackoff
{
    static void backoff()
    {
#if defined(__APPLE__) || defined(__linux__)
        usleep(0);
#elif defined(_WIN32)
        Sleep(0);
#else
#error "Unsupported OS"
#endif
    }
};

struct EmptyBackoff
{
    static void backoff()
    {
    }
};

// MSVC disallows zero-sized arrays in the middle of structs.
template <size_t alignment>
struct PaddedAtomic
{
    std::atomic<size_t> v;
    char padding[alignment - sizeof(size_t)];
};

template <>
struct PaddedAtomic<sizeof(size_t)>
{
    std::atomic<size_t> v;
};

template <typename Backoff, size_t alignment, bool withLoad>
struct SpinLock
{
    PaddedAtomic<alignment> flag;

    SpinLock()
    {
        flag.v = 0;
    }

    int lock()
    {
        int retryCount = 0;
        while (true) {
            size_t expected = 0;
            if (flag.v.compare_exchange_weak(expected, 1, std::memory_order_acq_rel)) {
                break;
            }
            if (withLoad) {
                while (flag.v.load(std::memory_order_acquire) != 0) {
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
        flag.v.store(0, std::memory_order_release);
    }
};

template <typename Backoff, size_t alignment>
struct TicketLock
{
    PaddedAtomic<alignment> in;
    PaddedAtomic<alignment> out;

    TicketLock()
    {
        in.v = 0;
        out.v = 0;
    }

    int lock()
    {
        int retryCount = 0;
        size_t ticket = in.v.fetch_add(1, std::memory_order_acquire);
        while (out.v.load(std::memory_order_acquire) != ticket) {
            Backoff::backoff();
            retryCount++;
        }
        return retryCount;
    }

    void unlock()
    {
        out.v.fetch_add(1, std::memory_order_release);
    }
};

struct UnlockedWorkData
{
    std::vector<unsigned> input;
    std::vector<unsigned> output;

    UnlockedWorkData(const std::vector<unsigned>& sourceInput)
        : input(sourceInput)
    {
        output.reserve(input.size());
    }

    bool popInput(unsigned& param, size_t& index, uint64_t& /*retryCount*/)
    {
        if (input.empty()) {
            return false;
        }
        param = input.back();
        input.pop_back();
        index = input.size();
        return true;
    }

    void pushOutput(unsigned* values, size_t numValues, uint64_t& /*retryCount*/)
    {
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

    bool popInput(unsigned& param, size_t& index, uint64_t& retryCount)
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

    void pushOutput(unsigned* values, size_t numValues, uint64_t& retryCount)
    {
        retryCount += outputLock.lock();
        output.insert(output.end(), values, values + numValues);
        outputLock.unlock();
    }
};

struct LockFreeWorkData
{
    PaddedAtomic<CACHE_LINE_WIDTH> inputIndex;
    std::vector<unsigned> input;
    char padding1[CACHE_LINE_WIDTH - sizeof(size_t)];

    PaddedAtomic<CACHE_LINE_WIDTH> outputIndex;
    std::vector<unsigned> output;
    char padding2[CACHE_LINE_WIDTH - sizeof(size_t)];

    LockFreeWorkData(const std::vector<unsigned>& sourceInput)
    {
        input = sourceInput;
        inputIndex.v = sourceInput.size();
        outputIndex.v = 0;
        output.resize(input.size());
    }

    bool popInput(unsigned& param, size_t& index, uint64_t& /*retryCount*/)
    {
        size_t curIndex = inputIndex.v.fetch_sub(1, std::memory_order_relaxed);
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

    void pushOutput(unsigned* values, size_t numValues, uint64_t& /*retryCount*/)
    {
        size_t curIndex = outputIndex.v.fetch_add(numValues, std::memory_order_relaxed);
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
        input.push_back(r * r);
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
    uint64_t retryCount;
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
    uint64_t retryCount = 0;
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
    uint64_t timeMs;
    // The average run lengths of all the threads.
    float avgRunLength;
    // The total retry count of all the threads.
    uint64_t totalRetryCount;

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

    int64_t startTime = getTimeCounter();
    for (int i = 0; i < numThreads; i++) {
        startSemaphore.post();
    }
    for (std::thread& thread: threads) {
        thread.join();
    }

    stats.timeMs = (getTimeCounter() - startTime) * 1000 / getTimeFreq();

    for (int i = 0; i < numThreads; i++) {
        stats.avgRunLength += threadStats[i].avgRunLength;
        stats.totalRetryCount += threadStats[i].retryCount;
    }
    stats.avgRunLength /= numThreads;

    // Check that we've successfully completed all work items.
    unsigned outputValue = 0;
    for (unsigned& v : data.output) {
        outputValue += v;
    }
    if (outputValue != targetValue) {
        printf("ERROR: Target value %u, output value %u, exiting.\n", targetValue, outputValue);
        exit(1);
    }
}

template <typename WorkData>
void run(const char* name, const char* methodName, int numThreads, int inputSize, unsigned workAmount)
{
    if (methodName != nullptr && strcmp(name, methodName) != 0) {
        return;
    }
    
    std::vector<unsigned> input;
    generateWork(inputSize, workAmount, input);
    unsigned targetValue = 0;
    for (int param : input) {
        targetValue += doWork(param);
    }

    const int numTimes = 50;
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
            name, (int)minStat.timeMs, (int)p50Stat.timeMs, (int)p90Stat.timeMs, (int)p98Stat.timeMs,
            minStat.avgRunLength, p50Stat.avgRunLength, p90Stat.avgRunLength, p98Stat.avgRunLength,
            (long long)minStat.totalRetryCount, (long long)p50Stat.totalRetryCount, (long long)p90Stat.totalRetryCount,
            (long long)p98Stat.totalRetryCount);
}

int main(int argc, char** argv)
{
    // Descriptions of args:
    //  * numThreads: self-descriptive;
    //  * workAmount: the average length of work simulation loop in doWork will be workAmount ^ 2
    //    (use 1 for trivial one-iteration loops, use 5 for 25 iteration loops, 10 for 1000 loops).
    //  * inputSize: all threads get trivial pieces of work from preallocated array, inputSize is the length of
    //    that array;
    if (argc < 3 || argc > 5) {
        printf("Usage: %s numThreads workAmount [method [inputSize]]\n", argv[0]);
        return 1;
    }
    int numThreads = atoi(argv[1]);
    unsigned workAmount = atoi(argv[2]);
    const char* method = argc >= 4 ? argv[3] : nullptr;
    int inputSize = (argc == 5) ? atoi(argv[4]) : 20000000 / (workAmount * workAmount);

    printf("Num threads: %d, input %d-%d size %d\n", numThreads, workAmount - 1, workAmount + 1, inputSize);

    if (numThreads == 1) {
        run<UnlockedWorkData>("no lock", method, numThreads, inputSize, workAmount);
    }
    run<LockFreeWorkData>("lock-free", method, numThreads, inputSize, workAmount);
    run<LockingWorkData<SpinLock<EmptyBackoff, CACHE_LINE_WIDTH, true>>>("spinlock", method, numThreads, inputSize, workAmount);
#if !defined(_MSC_VER)
    run<LockingWorkData<SpinLock<PauseBackoff, CACHE_LINE_WIDTH, true>>>("spinlock+pause", method, numThreads, inputSize, workAmount);
    run<LockingWorkData<SpinLock<NopBackoff, CACHE_LINE_WIDTH, true>>>("spinlock+nop", method, numThreads, inputSize, workAmount);
#endif
    run<LockingWorkData<SpinLock<SchedBackoff, CACHE_LINE_WIDTH, true>>>("spinlock+yield", method, numThreads, inputSize, workAmount);
    run<LockingWorkData<SpinLock<SleepBackoff, CACHE_LINE_WIDTH, true>>>("spinlock+sleep", method, numThreads, inputSize, workAmount);
    run<LockingWorkData<SpinLock<EmptyBackoff, CACHE_LINE_WIDTH, false>>>("spinlock,no load loop", method, numThreads, inputSize, workAmount);
#if !defined(_MSC_VER)
    run<LockingWorkData<SpinLock<PauseBackoff, CACHE_LINE_WIDTH, false>>>("spinlock+pause,no load loop", method, numThreads, inputSize, workAmount);
#endif
    run<LockingWorkData<SpinLock<EmptyBackoff, sizeof(size_t), true>>>("spinlock,unaligned", method, numThreads, inputSize, workAmount);
#if !defined(_MSC_VER)
    run<LockingWorkData<SpinLock<PauseBackoff, sizeof(size_t), true>>>("spinlock+pause,unaligned", method, numThreads, inputSize, workAmount);
#endif
    run<LockingWorkData<TicketLock<EmptyBackoff, CACHE_LINE_WIDTH>>>("ticketlock", method, numThreads, inputSize, workAmount);
    run<LockingWorkData<TicketLock<EmptyBackoff, sizeof(size_t)>>>("ticketlock,unaligned", method, numThreads, inputSize, workAmount);
    // std::mutex is horribly slow on OS X, skip it altogether
#if !defined(__APPLE__)
    run<LockingWorkData<MutexLock>>("std::mutex", method, numThreads, inputSize, workAmount);
#endif
    run<LockingWorkData<SemaphoreLock>>("semaphore", method, numThreads, inputSize, workAmount);

    return 0;
}
