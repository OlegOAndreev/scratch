#include "common.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

template<typename T>
struct Padded : public T {
    static size_t const kPaddingSize = 128;
    char padding[kPaddingSize];
};

template<typename T>
void testSemaphoresImpl(int numThreads, const char* name)
{
    int const kIters = 1000000;

    std::unique_ptr<T[]> semaphores{new T[numThreads]};
    std::vector<std::unique_ptr<std::thread>> threads;
    std::vector<int> times;
    times.resize(numThreads);
    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back(new std::thread([=, &semaphores, &times] {
            T& semaphore = semaphores[i];
            T& nextSemaphore = semaphores[(i + 1) % numThreads];
            uint64_t startTime = getTimeTicks();
            for (int k = 0; k < kIters; k += numThreads) {
                semaphore.wait();
                nextSemaphore.post();
            }
            times[i] = elapsedMsec(startTime);
        }));
    }
    semaphores[0].post();

    for (int i = 0; i < numThreads; i++) {
        threads[i]->join();
    }

    int maxTime = times[0];
    for (int i = 1; i < numThreads; i++) {
        maxTime = std::max(maxTime, times[i]);
    }
    if (maxTime == 0) {
        maxTime = 1;
    }
    int64_t perSec = (int64_t)kIters * 1000 / maxTime;
    printf("%s: %lld posts per second (with %d threads ping-pong)\n", name, (long long)perSec,
           numThreads);
}

struct CondVarSemaphore {
    void post()
    {
        std::lock_guard<std::mutex> lk{mutex};
        value++;
        condVar.notify_one();
    }

    void wait()
    {
        while (true) {
            std::unique_lock<std::mutex> lk{mutex};
            condVar.wait(lk, [&] { return value > 0; });
            value--;
            return;
        }
    }

    std::condition_variable condVar;
    std::mutex mutex;
    int value = 0;
};

struct SpinSemaphore {
    void post()
    {
        v.fetch_add(1, std::memory_order_seq_cst);
    }

    void wait()
    {
        while (true) {
            int old = v.load(std::memory_order_relaxed);
            if (old > 0) {
                if (v.compare_exchange_weak(old, old - 1, std::memory_order_seq_cst)) {
                    return;
                }
            }
        }
    }

    std::atomic<int> v{0};
};

void testSemaphores(int numThreads)
{
    testSemaphoresImpl<Semaphore>(1, "Semaphore");
    if (numThreads > 1) {
        testSemaphoresImpl<Semaphore>(numThreads, "Semaphore");
        testSemaphoresImpl<Padded<Semaphore>>(numThreads, "Padded<Semaphore>");
    }

    testSemaphoresImpl<CondVarSemaphore>(1, "std::condition_variable");
    if (numThreads > 1) {
        testSemaphoresImpl<CondVarSemaphore>(numThreads, "std::condition_variable");
        testSemaphoresImpl<Padded<CondVarSemaphore>>(numThreads, "Padded<std::condition_variable>");
    }

    testSemaphoresImpl<SpinSemaphore>(1, "SpinSemaphore");
    if (numThreads > 1) {
        testSemaphoresImpl<SpinSemaphore>(numThreads, "SpinSemaphore");
        testSemaphoresImpl<Padded<SpinSemaphore>>(numThreads, "Padded<SpinSemaphore>");
    }
}
