#pragma once

#include <atomic>

#include "common.h"

// A synchronization primitive for waiting until the target count value is reached (like CountDownLatch in Java).
class CountWaiter {
public:
    CountWaiter(int targetCount)
        : counter(targetCount)
    {
    }

    ~CountWaiter()
    {
    }

    // Posts (i.e. increases) the count value by the given amount. Returns true if the waiter have been awoken
    // by this post.
    bool post(int count = 1)
    {
        if (counter.fetch_sub(count, std::memory_order_acq_rel) > count) {
            return false;
        }

        int numWakeup = numWaiters.load(std::memory_order_relaxed);

        for (int i = 0; i < numWakeup; i++) {
            semaphore.post();
        }

        return true;
    }

    // Waits until the target count value has been hit.
    void wait()
    {
        if (counter.load(std::memory_order_acquire) <= 0) {
            return;
        }

        numWaiters.fetch_add(1, std::memory_order_relaxed);

        // Recheck that the condition has not been met.
        if (counter.load(std::memory_order_acquire) > 0) {
            semaphore.wait();
        }

        numWaiters.fetch_sub(1, std::memory_order_relaxed);
    }

private:
    // counter is decreasing from targetCount to zero.
    std::atomic<int> counter;
    std::atomic<int> numWaiters{0};
    Semaphore semaphore;
};
