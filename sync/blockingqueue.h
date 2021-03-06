#pragma once

#include "common.h"

#include <atomic>
#include <utility>

// Blocking queue based on non-blocking queue (BaseQueueType). Uses semaphore to sleep
// when there queue is empty. BaseQueueType must have two methods:
//  * bool enqueue(U&&)
//  * bool dequeue(T&)
// and have one typedef:
//  * ElementType (alias to T)
template<typename BaseQueueType>
class BlockingQueue {
public:
    using T = typename BaseQueueType::ElementType;

    // All the arguments are passed to the BaseQueueType constructor.
    template<typename... Args>
    BlockingQueue(Args&&... args);

    // Enqueues element, returns false if the queue is full and the enqueue failed. Universal
    // reference is the easiest way to support both T const& and T&& arguments.
    template<typename U>
    bool enqueue(U&& u);

    // Dequeues the element, blocking if the queue is empty. Returns false if the queue has been
    // closed.
    bool dequeue(T& t);

    // Tries to dequeue the element. Returns true if dequeue succeeded.
    bool tryDequeue(T& t);

    // Closes the queue: no elements can be enqueued, dequeue() returns with false.
    void close();
    bool isClosed() const;

private:
    BaseQueueType baseQueue;
    std::atomic<int> numSleepingConsumers{0};
    std::atomic<bool> closed{false};
    Semaphore sleepingSemaphore;
};

template<typename BaseQueueType>
template<typename... Args>
BlockingQueue<BaseQueueType>::BlockingQueue(Args&&... args)
    : baseQueue(std::forward<Args>(args)...)
{
}

template<typename BaseQueueType>
template<typename U>
bool BlockingQueue<BaseQueueType>::enqueue(U&& u)
{
    if (!baseQueue.enqueue(std::forward<U>(u))) {
        return false;
    }
    // NOTE: There is a non-obvious race condition here: if the queue is empty and thread 1
    // (consumer) is trying to sleep after checking that it is empty and thread 2 is trying to add
    // the new element, the following can potentially happen:
    // * Thread 1:
    //   1. checks that queue is empty (passes)
    //   2. increments numSleepingConsumers (0 -> 1)
    //   3. checks that queue is empty
    // * Thread 2:
    //   1. adds new item to the queue (queue becomes non-empty)
    //   2. reads numSleepingConsumers
    //
    // Original solution to this problem by using acq_rel/relaxed when writing/reading
    // numSleepingConsumers and acq_rel (via atomic::exchange RMW) when updating cell.sequence_ in
    // mpmc_bounded_queue::dequeue. This has been based on the reasoning that acquire and release
    // match the LoadLoad+LoadStore and LoadStore+StoreStore barrier correspondingly and acq_rel RMW
    // is, therefore, a total barrier. However, that is not what part 1.10 of the C++ standard says
    // as discussed in
    // https://stackoverflow.com/questions/52606524/what-exact-rules-in-the-c-memory-model-prevent-reordering-before-acquire-opera/
    // The easiest fix is simply changing all the related accesses to seq_cst:
    //  * all reads and writes on numSleepingConsumers
    //  * first read in dequeue and last write in enqueue. The good thing is that the generated code
    //  for
    //    acq_rel RMW is identical to seq_cst store on the most relevant platforms (x86-64 and
    //    aarch64). Similar idea has been noted in
    //    http://cbloomrants.blogspot.com/2011/07/07-31-11-example-that-needs-seqcst_31.html
    if (numSleepingConsumers.load(std::memory_order_seq_cst) > 0) {
        sleepingSemaphore.post();
    }
    return true;
}

template<typename BaseQueueType>
bool BlockingQueue<BaseQueueType>::dequeue(T& t)
{
    if (baseQueue.dequeue(t)) {
        return true;
    }
    // Do not load closed before trying to dequeue (remove useless load in happy case).
    if (closed.load(std::memory_order_seq_cst)) {
        // Always re-check the queue after checking closed flag. NOTE: This check requires that
        // there is a barrier between the last enqueue() and close().
        return baseQueue.dequeue(t);
    }

    int const kSpinCount = 100;

    while (true) {
        // Spin for a few iterations.
        for (int i = 0; i < kSpinCount; i++) {
            if (baseQueue.dequeue(t)) {
                return true;
            }
        }

        // Sleep until the new element arrives.
        numSleepingConsumers.fetch_add(1, std::memory_order_seq_cst);

        // Recheck that there are still no elements in the queue. This is used to prevent
        // the race condition, where the pauses between checking the queue first
        // and incrementing the numSleepingConsumers, while the element is enqueued during
        // this pause. See above NOTE on checking closed flag before trying to dequeue.
        bool gotClosed = closed.load(std::memory_order_seq_cst);
        if (baseQueue.dequeue(t)) {
            numSleepingConsumers.fetch_sub(1, std::memory_order_seq_cst);
            return true;
        } else if (gotClosed) {
            numSleepingConsumers.fetch_sub(1, std::memory_order_seq_cst);
            return false;
        }

        sleepingSemaphore.wait();
        numSleepingConsumers.fetch_sub(1, std::memory_order_seq_cst);
    }
}

template<typename BaseQueueType>
bool BlockingQueue<BaseQueueType>::tryDequeue(T& t)
{
    return baseQueue.dequeue(t);
}

// Close must be called only after the latest enqueue, otherwise it is possible that queue elements
// will be lost.
template<typename BaseQueueType>
void BlockingQueue<BaseQueueType>::close()
{
    // This store is required to be seq_cst for the same reasons as in dequeue, to prevent StoreLoad
    // reordering.
    closed.store(true, std::memory_order_seq_cst);
    int wakeNum = numSleepingConsumers.load(std::memory_order_seq_cst);
    for (int i = 0; i < wakeNum; i++) {
        sleepingSemaphore.post();
    }
}

template<typename BaseQueueType>
bool BlockingQueue<BaseQueueType>::isClosed() const
{
    return closed.load(std::memory_order_seq_cst);
}
