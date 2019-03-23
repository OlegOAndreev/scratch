#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "common.h"
#include "mpmc_bounded_queue/mpmc_bounded_queue.h"

template<typename Task>
class FiberWorkStealingPool {
public:
    FiberWorkStealingPool();
    FiberWorkStealingPool(int numThreads);
    ~FiberWorkStealingPool();

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

    void workerMain(int threadNum);

    template<typename F>
    bool tryToPushTask(F&& f, int& threadToPush);
    bool tryToStealTask(Task& task, int& threadToSteal);
};

template<typename Task>
FiberWorkStealingPool<Task>::FiberWorkStealingPool()
    : FiberWorkStealingPool(std::thread::hardware_concurrency())
{
}

template<typename Task>
FiberWorkStealingPool<Task>::FiberWorkStealingPool(int numThreads)
    : workerThreads(new PerThread[numThreads])
    , workerThreadsSize(numThreads)
{
    for (int i = 0; i < numThreads; i++) {
        workerThreads[i].thread = std::thread([this, i] { workerMain(i); });
    }
}

template<typename Task>
FiberWorkStealingPool<Task>::~FiberWorkStealingPool()
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
int FiberWorkStealingPool<Task>::numThreads() const
{
    return workerThreadsSize;
}

template<typename Task>
template<typename F>
void FiberWorkStealingPool<Task>::submit(F&& f)
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
        sleepingSemaphore.post();
    }
}

template<typename Task>
template<typename F>
void FiberWorkStealingPool<Task>::submitRange(F&& f, size_t from, size_t to)
{
    const size_t kMinGranularity = 16;

    // We do not care about synchronization too much here: lastPushedThread is generally used to approximately
    // load-balance the worker threads.
    int threadToPush = lastPushedThread.load(std::memory_order_relaxed);

    // Try to split all work so that there are least worker threads * 4 pieces for proper load balancing.
    size_t submitGranularity = std::max((to - from) / (workerThreadsSize * 4), kMinGranularity);
    size_t submitted = 0;
    int submittedTasks = 0;
    for (submitted = from; submitted < to; submitted += submitGranularity) {
        threadToPush++;
        if (threadToPush >= workerThreadsSize) {
            threadToPush = 0;
        }
        size_t n = std::min(to - submitted, submitGranularity);
        if (!tryToPushTask([f, submitted, n] { f(submitted, submitted + n); }, threadToPush)) {
            break;
        }
        submittedTasks++;
    }
    if (submitted < to) {
        // Extremely unlikely: the queue is full, just run the task in the caller.
        f(submitted, to);
    }
    lastPushedThread.store(threadToPush, std::memory_order_relaxed);

    // NOTE: See submit() for description of synchronization here.
    int sleeping = numSleepingWorkers.load(std::memory_order_seq_cst);
    if (sleeping > 0) {
        int toWake = std::min(sleeping, submittedTasks);
        for (int i = 0; i < toWake; i++) {
            sleepingSemaphore.post();
        }
    }
}

template<typename Task>
void FiberWorkStealingPool<Task>::workerMain(int threadNum)
{
    PerThread& thisThread = workerThreads[threadNum];
    int threadToSteal = (threadNum + 1) % workerThreadsSize;

    Task task;
    while (!thisThread.stopFlag.load(std::memory_order_relaxed)) {
        // Prefer dequeueing tasks for this thread first.
        if (thisThread.queue.dequeue(task)) {
            task();
            continue;
        }

        int const kSpinCount = 100;

        // Spin for a few iterations.
        bool foundTask = false;
        for (int i = 0; i < kSpinCount; i++) {
            if (tryToStealTask(task, threadToSteal)) {
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
        if (tryToStealTask(task, threadToSteal)) {
            numSleepingWorkers.fetch_sub(1, std::memory_order_seq_cst);
            task();
        } else {
            sleepingSemaphore.wait();
            numSleepingWorkers.fetch_sub(1, std::memory_order_seq_cst);
        }
    }

    thisThread.stopped.store(true, std::memory_order_relaxed);
}

// Try to push task from functor f at least to one thread queue, starting with threadToPush. Updates
template<typename Task>
template<typename F>
bool FiberWorkStealingPool<Task>::tryToPushTask(F&& f, int& threadToPush)
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
bool FiberWorkStealingPool<Task>::tryToStealTask(Task& task, int& threadToSteal)
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
