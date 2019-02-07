#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "common.h"
#include "mpmc_bounded_queue/mpmc_bounded_queue.h"

#define WORK_STEALING_STATS

// A simple work-stealing threadpool implementation with no task dependencies and no futures.
// Task should have a void operator()().
template<typename Task>
class SimpleWorkStealingPoolImpl {
public:
    SimpleWorkStealingPoolImpl();
    SimpleWorkStealingPoolImpl(int numThreads);
    ~SimpleWorkStealingPoolImpl();

    // Submits the task for execution in the pool (f must be a callable of type void()).
    template<typename F>
    void submit(F&& f);

    // Submits a number of ranged tasks for range [from, to) for execution in the pool,
    // i.e. splits the range [from, to) into subranges [from, r1), [r1, r2)
    // and calls f for each: f(from, r1), f(r1, r2), ... (f must be a callable of type void(size_t, size_t)).
    template<typename F>
    void submitRange(F&& f, size_t from, size_t to);

    // Returns the number of worker threads in the pool.
    int numThreads() const;

#if defined(WORK_STEALING_STATS)
    std::atomic<uint64_t> totalSemaphorePosts{0};
    char padding1[64];
    std::atomic<uint64_t> totalSemaphoreWaits{0};
    char padding2[64];
    std::atomic<uint64_t> totalTrySteals{0};
    char padding3[64];
    std::atomic<uint64_t> totalSteals{0};
    char padding4[64];
#endif

private:
    struct PerThread {
        const size_t kMaxTasksInQueue = 32 * 1024;

        std::thread thread;
        mpmc_bounded_queue<Task> queue{kMaxTasksInQueue};
        // mpmc_bounded_queue is already padded.
        std::atomic<bool> stopFlag{false};
        std::atomic<bool> stopped{false};
    };

    // Using std::vector is too painful with mpmc_queue or std::atomic.
    std::unique_ptr<PerThread[]> workerThreads;
    int workerThreadsSize;

    std::atomic<int> numSleepingWorkers{0};
    Semaphore sleepingSemaphore;

    std::atomic<int> lastPushedThread{0};

    void forkJoinWorkerMain(int threadNum);

    template<typename F>
    bool tryToPushTask(F&& f, int& threadToPush);
    bool tryToStealTask(Task& task, int& threadToSteal);
};

template<typename Task>
SimpleWorkStealingPoolImpl<Task>::SimpleWorkStealingPoolImpl()
    : SimpleWorkStealingPoolImpl(std::thread::hardware_concurrency())
{
}

template<typename Task>
SimpleWorkStealingPoolImpl<Task>::SimpleWorkStealingPoolImpl(int numThreads)
    : workerThreads(new PerThread[numThreads])
    , workerThreadsSize(numThreads)
{
    for (int i = 0; i < numThreads; i++) {
        workerThreads[i].thread = std::thread([this, i] { forkJoinWorkerMain(i); });
    }
}

template<typename Task>
SimpleWorkStealingPoolImpl<Task>::~SimpleWorkStealingPoolImpl()
{
    for (int i = 0; i < workerThreadsSize; i++) {
        workerThreads[i].stopFlag.store(true, std::memory_order_relaxed);
    }

    int numStoppedThreads = 0;
    while (numStoppedThreads != workerThreadsSize) {
        for (int i = 0; i < workerThreadsSize; i++) {
            if (workerThreads[i].stopped.load(std::memory_order_relaxed) && workerThreads[i].thread.joinable()) {
                workerThreads[i].thread.join();
                numStoppedThreads++;
            } else {
                sleepingSemaphore.post();
            }
        }
    }
}

template<typename Task>
int SimpleWorkStealingPoolImpl<Task>::numThreads() const
{
    return workerThreadsSize;
}

template<typename Task>
template<typename F>
void SimpleWorkStealingPoolImpl<Task>::submit(F&& f)
{
    // We do not care about synchronization too much here: lastPushedThread is generally used to approximately
    // load-balance the worker threads.
    int threadToPush = (lastPushedThread.load(std::memory_order_relaxed) + 1) % workerThreadsSize;
    if (!tryToPushTask(f, threadToPush)) {
        // Extremely unlikely: the queue is full, just run the task in the caller.
        f();
        return;
    }
    lastPushedThread.store(threadToPush, std::memory_order_relaxed);
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
    // The easiest fix is simply changing all the related accesses to seq_cst:
    //  * all reads and writes on numSleepingWorkers
    //  * first read in dequeue and last write in enqueue.
    // The good thing is that the generated code for acq_rel RMW is identical to seq_cst store on the relevant
    // platforms (x86-64 and aarch64).
    //
    // Copied from mpmcblockingtaskqueue.h
    if (numSleepingWorkers.load(std::memory_order_seq_cst) > 0) {
#if defined(WORK_STEALING_STATS)
        totalSemaphorePosts.fetch_add(1, std::memory_order_relaxed);
#endif
        sleepingSemaphore.post();
    }
}

template<typename Task>
template<typename F>
void SimpleWorkStealingPoolImpl<Task>::submitRange(F&& f, size_t from, size_t to)
{
    const size_t kMinGranularity = 16;

    // We do not care about synchronization too much here: lastPushedThread is generally used to approximately
    // load-balance the worker threads.
    int threadToPush = lastPushedThread.load(std::memory_order_relaxed);

    // Try to split all work so that there are least worker threads * 4 pieces for proper load balancing.
    size_t pushGranularity = std::max((to - from) / (workerThreadsSize * 4), kMinGranularity);
    size_t pushed = 0;
    int pushedTasks = 0;
    for (pushed = from; pushed < to; pushed += pushGranularity) {
        threadToPush++;
        if (threadToPush >= workerThreadsSize) {
            threadToPush = 0;
        }
        size_t n = std::min(to - pushed, pushGranularity);
        if (!tryToPushTask([f, pushed, n] { f(pushed, pushed + n); }, threadToPush)) {
            break;
        }
        pushedTasks++;
    }
    if (pushed < to) {
        // Extremely unlikely: the queue is full, just run the task in the caller.
        f(pushed, to);
    }
    lastPushedThread.store(threadToPush, std::memory_order_relaxed);

    // NOTE: See submit() for description of synchronization here.
    int sleeping = numSleepingWorkers.load(std::memory_order_seq_cst);
    if (sleeping > 0) {
#if defined(WORK_STEALING_STATS)
        totalSemaphorePosts.fetch_add(1, std::memory_order_relaxed);
#endif
        int toWake = std::min(sleeping, pushedTasks);
        for (int i = 0; i < toWake; i++) {
            sleepingSemaphore.post();
        }
    }
}

template<typename Task>
void SimpleWorkStealingPoolImpl<Task>::forkJoinWorkerMain(int threadNum)
{
    PerThread& thisThread = workerThreads[threadNum];
    int threadToSteal = (threadNum + 1) % workerThreadsSize;
#if defined(WORK_STEALING_STATS)
    uint64_t localSemaphoreWaits = 0;
    uint64_t localTrySteals = 0;
    uint64_t localSteals = 0;
#endif

    Task task;
    while (!thisThread.stopFlag.load(std::memory_order_relaxed)) {
        // Prefer dequeueing tasks for this thread first.
        if (thisThread.queue.dequeue(task)) {
            task();
            continue;
        }

        int const kSpinCount = 1000;

        // Spin for a few iterations
        bool foundTask = false;
        for (int i = 0; i < kSpinCount; i++) {
#if defined(WORK_STEALING_STATS)
            localTrySteals++;
#endif
            if (tryToStealTask(task, threadToSteal)) {
#if defined(WORK_STEALING_STATS)
                localSteals++;
#endif
                task();
                foundTask = true;
                break;
            }
        }
        if (foundTask) {
            continue;
        }

        // Sleep until the new task arrives.
        numSleepingWorkers.fetch_add(1, std::memory_order_seq_cst);

        // Recheck that there are still no tasks in the queues. This is used to prevent the race condition,
        // where the pauses between checking the queue first and incrementing the numSleepingWorkers, while the task
        // is submitted during this pause.
        // NOTE: See NOTE in the submit for the details on correctness of the sleep.
        //
        // Copied from mpmcblockingtaskqueue.h
#if defined(WORK_STEALING_STATS)
            localTrySteals++;
#endif
        if (tryToStealTask(task, threadToSteal)) {
#if defined(WORK_STEALING_STATS)
                localSteals++;
#endif
            numSleepingWorkers.fetch_sub(1, std::memory_order_seq_cst);
            task();
        } else {
#if defined(WORK_STEALING_STATS)
                localSemaphoreWaits++;
#endif
            sleepingSemaphore.wait();
            numSleepingWorkers.fetch_sub(1, std::memory_order_seq_cst);
        }
    }

#if defined(WORK_STEALING_STATS)
    totalSemaphoreWaits.fetch_add(localSemaphoreWaits, std::memory_order_relaxed);
    totalTrySteals.fetch_add(localTrySteals, std::memory_order_relaxed);
    totalSteals.fetch_add(localSteals, std::memory_order_relaxed);
#endif

    thisThread.stopped.store(true, std::memory_order_relaxed);
}

// Try to push task from functor f at least to one thread queue, starting with threadToPush. Updates
template<typename Task>
template<typename F>
bool SimpleWorkStealingPoolImpl<Task>::tryToPushTask(F&& f, int& threadToPush)
{
    int t = threadToPush;
    if (workerThreads[t].queue.enqueue(Task(std::forward<F>(f)))) {
        return true;
    }

    for (int i = t + 1; i < workerThreadsSize; i++) {
        if (workerThreads[i].queue.enqueue(Task(std::forward<F>(f)))) {
            threadToPush = i;
            return true;
        }
    }
    for (int i = 0; i < t; i++) {
        if (workerThreads[i].queue.enqueue(Task(std::forward<F>(f)))) {
            threadToPush = i;
            return true;
        }
    }
    return false;
}

// Try to steal a task from any thread, starting with threadToSteal. Updates both task and threadToSteal.
// Returns true if a task has been successfully stolen, false otherwise.
template<typename Task>
bool SimpleWorkStealingPoolImpl<Task>::tryToStealTask(Task& task, int& threadToSteal)
{
    int t = threadToSteal;
    if (workerThreads[t].queue.dequeue(task)) {
        return true;
    }
    for (int i = t; i < workerThreadsSize; i++) {
        if (workerThreads[i].queue.dequeue(task)) {
            threadToSteal = i;
            return true;
        }
    }
    for (int i = 0; i < t; i++) {
        if (workerThreads[i].queue.dequeue(task)) {
            threadToSteal = i;
            return true;
        }
    }
    return false;
}
