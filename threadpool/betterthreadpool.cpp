#include <thread>
#include <vector>

#include "common.h"

#include "betterthreadpool.h"
#include "mpmc_bounded_queue.h"

namespace detail {

size_t const kMaxTasksInQueue = 256 * 1024;
int const kSpinCount = 100;

struct BetterThreadPoolImpl {
    std::vector<std::thread> workerThreads;
    mpmc_bounded_queue<std::packaged_task<void()>> workerQueue;
    std::atomic<int> numSleepingWorkers{0};
    Semaphore sleepingSemaphore;
    std::atomic<bool> stopFlag{false};
    std::atomic<int> numStoppedWorkers{0};

    BetterThreadPoolImpl();
    ~BetterThreadPoolImpl();
};

BetterThreadPoolImpl::BetterThreadPoolImpl()
    : workerQueue(kMaxTasksInQueue)
{
}

BetterThreadPoolImpl::~BetterThreadPoolImpl()
{
}

void betterWorkerMain(BetterThreadPoolImpl* impl)
{
    std::packaged_task<void()> task;
    while (true) {
        if (impl->stopFlag.load(std::memory_order_relaxed)) {
            break;
        }
        // Spin for a few iterations
        bool gotTask = false;
        for (int i = 0; i < kSpinCount; i++) {
            if (impl->workerQueue.dequeue(task)) {
                gotTask = true;
                break;
            }
        }
        // If we got the task, run it and check the stop flag and try searching the next task again.
        if (gotTask) {
            task();
            continue;
        }

        // Sleep until the new task arrives.
        impl->numSleepingWorkers.fetch_add(1, std::memory_order_seq_cst);

        // Recheck that there are still no tasks in the queue. This is used to prevent the race condition,
        // where the pauses between checking the queue first and incrementing the numSleepingWorkers, while the task
        // is submitted during this pause.
        // NOTE: See NOTE in the submitImpl for the details on correctness of the sleep.
        if (impl->workerQueue.dequeue(task)) {
            impl->numSleepingWorkers.fetch_add(-1, std::memory_order_seq_cst);
            task();
        } else {
            impl->sleepingSemaphore.wait();
            impl->numSleepingWorkers.fetch_add(-1, std::memory_order_seq_cst);
        }
    }
    impl->numStoppedWorkers.fetch_add(1, std::memory_order_relaxed);
}

}

BetterThreadPool::BetterThreadPool()
    : BetterThreadPool(std::thread::hardware_concurrency())
{
}

BetterThreadPool::BetterThreadPool(int numThreads)
    : impl(new detail::BetterThreadPoolImpl{})
{
    impl->workerThreads.resize(numThreads);
    for (int i = 0; i < numThreads; i++) {
        impl->workerThreads[i] = std::thread(detail::betterWorkerMain, impl);
    }
}

BetterThreadPool::~BetterThreadPool()
{
    // Set stopFlag and wait until all the instances signal
    impl->stopFlag.store(true, std::memory_order_relaxed);
    int numWorkers = impl->workerThreads.size();
    while (impl->numStoppedWorkers.load(std::memory_order_relaxed) != numWorkers) {
        impl->sleepingSemaphore.post();
    }

    for (std::thread& t : impl->workerThreads) {
        t.join();
    }

    delete impl;
}

int BetterThreadPool::numThreads() const
{
    return impl->workerThreads.size();
}

void BetterThreadPool::submitImpl(std::packaged_task<void()>&& task)
{
    if (!impl->workerQueue.enqueue(std::move(task))) {
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
    if (impl->numSleepingWorkers.load(std::memory_order_seq_cst) > 0) {
        impl->sleepingSemaphore.post();
    }
}
