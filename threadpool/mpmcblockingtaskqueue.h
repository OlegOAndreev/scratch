#pragma once

#include "mpmc_bounded_queue/mpmc_bounded_queue.h"

#include "common.h"

// Blocking task queue based on mpmc_bounded_queue + semaphore for sleeping when the tasks are absent.
template<typename Task>
class MpMcBlockingTaskQueue {
public:
    bool push(Task&& task)
    {
        if (!workerQueue.enqueue(std::move(task))) {
            return false;
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
        // The easiest fix is simply changing all the related accesses to seq_cst:
        //  * all reads and writes on numSleepingWorkers
        //  * first read in dequeue and last write in enqueue.
        // The good thing is that the generated code for acq_rel RMW is identical to seq_cst store on the relevant
        // platforms (x86-64 and aarch64).
        if (numSleepingWorkers.load(std::memory_order_seq_cst) > 0) {
            sleepingSemaphore.post();
        }
        return true;
    }

    bool pop(Task& task)
    {
        int const kSpinCount = 1000;

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
    const size_t kMaxTasksInQueue = 32 * 1024;

    mpmc_bounded_queue<Task> workerQueue{kMaxTasksInQueue};
    std::atomic<int> numSleepingWorkers{0};
    Semaphore sleepingSemaphore;
    std::atomic<bool> stopFlag{false};
};
