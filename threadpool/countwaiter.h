#pragma once

#include <atomic>

#include "common.h"

// A structure for waiting until the count becomes zero (a la CountDownLatch in Java).
class CountWaiter {
public:
    CountWaiter(int targetCount)
        : counter(targetCount)
    {
    }

    void post(int count = 1)
    {
        // We can do a relaxed here, because a semaphore post-wait pair SHOULD be a rel-acq pair.
        if (counter.fetch_sub(count, std::memory_order_relaxed) <= count) {
            semaphore.post();
        }
    }

    void wait()
    {
        if (counter.load(std::memory_order_relaxed) > 0) {
            semaphore.wait();
        }
    }

private:
    std::atomic<int> counter;
    Semaphore semaphore;
};
