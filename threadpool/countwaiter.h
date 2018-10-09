#pragma once

#include <atomic>

#include "common.h"

// A structure for waiting until the count becomes zero (a la CountDownLatch in Java).
class CountWaiter {
public:
    CountWaiter(size_t targetCount)
        : counter(targetCount)
    {
    }

    void post()
    {
        // We can do a relaxed here, because a semaphore post-wait pair SHOULD be a rel-acq pair.
        if (counter.fetch_sub(1, std::memory_order_relaxed) == 1) {
            semaphore.post();
        }
    }

    void wait()
    {
        if (counter.load(std::memory_order_relaxed) != 0) {
            semaphore.wait();
        }
    }

private:
    std::atomic<size_t> counter;
    Semaphore semaphore;
};
