#pragma once

#include <cstddef>
#include <thread>

// Minimal thread pool, uses C++11 threading primitives.
class ThreadPool
{
public:
    ThreadPool()
        : ThreadPool(std::thread::hardware_concurrency())
    {
    }

    ThreadPool(int numThreads)
    {
    }
};
