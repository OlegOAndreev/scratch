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
        impl->numSleepingWorkers.fetch_add(1, std::memory_order_acq_rel);

        // Recheck that there are still no tasks in the queue. This is used to prevent the race condition,
        // where the pauses between checking the queue first and incrementing the numSleepingWorkers, while the task
        // is submitted during this pause.
        // NOTE: See NOTE in the submitImpl for the details on correctness of the sleep.
        if (impl->workerQueue.dequeue(task)) {
            impl->numSleepingWorkers.fetch_add(-1, std::memory_order_acq_rel);
            task();
        } else {
            impl->sleepingSemaphore.wait();
            impl->numSleepingWorkers.fetch_add(-1, std::memory_order_acq_rel);
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
    // If no barriers are present (e.g. all operations are memory_order_relaxed and there are no fences),
    // all the operations could happen in any order, therefore, thread 2 could read numSleepingWorkers = 0 on step 2,
    // while thread 1 could check that queue is empty on step 3. However, if the queue is empty, then both
    // workerQueue.dequeue() and workerQueue.enqueue() access the same cell (enqueue_pos_ is equal to dequeue_pos_)
    // and the following happens:
    //  Thread 1:
    //   1. cell.sequence_.load(acquire)
    //   2. numSleepingWorkers.increment(acq_rel)
    //   3. cell.sequence_.load(acquire)
    //  Thread 2:
    //   1. cell.sequence_.load(acquire)
    //   2. cell.sequence_.exchange(acq_rel)
    //   3. numSleepingWorkers.load(acquire)
    //
    // Now the read of numSleepingWorkers cannot reorder before the queue becomes non-empty and the second check
    // that the queue is non-empty cannot be reordered before increment numSleepingWorkers. Note, that original
    // mpmc_bounded_queue had store instead of exchange for the step 2 in thread 2:
    //   2. cell.sequence_.store(release)
    //   3. numSleepingWorkers.load(acquire)
    // Steps 2 and 3 could be reordered, so that thread 2 would load numSleepingWorkers before thread 1 incremented it.
    if (impl->numSleepingWorkers.load(std::memory_order_acquire) > 0) {
        impl->sleepingSemaphore.post();
    }
}
