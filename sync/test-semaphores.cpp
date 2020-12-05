#include "common.h"

#include "benaphore.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

template<typename T>
struct Padded {
    static size_t const kPaddingSize = 128;

    T sema;

    void post()
    {
        sema.post();
    }

    void wait()
    {
        sema.wait();
    }

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

template<int numSpins>
struct TrySemaphore {
    Semaphore sema;

    void post()
    {
        sema.post();
    }

    void wait()
    {
        for (int spins = 0; spins < numSpins; spins++) {
            if (sema.tryWait()) {
                return;
            }
        }
        sema.wait();
    }
};

template<bool notifyInLock>
struct CondVarSemaphore {
    void post()
    {
        std::unique_lock<std::mutex> lk{mutex};
        value++;
        if (!notifyInLock) {
            lk.unlock();
        }
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

template<int64_t numSpins>
struct SpinSemaphore {
    void post()
    {
        v.fetch_add(1, std::memory_order_seq_cst);
    }

    void wait()
    {
        for (int64_t spins = 0;; spins++) {
            int old = v.load(std::memory_order_seq_cst);
            // int old = v.load(std::memory_order_relaxed);
            if (old > 0) {
                if (v.compare_exchange_weak(old, old - 1, std::memory_order_seq_cst)) {
                    return;
                }
            }
            if (numSpins > 0) {
                if (spins > numSpins) {
                    std::this_thread::yield();
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
    printf("-----\n");

    testSemaphoresImpl<TrySemaphore<1000>>(1, "TrySemaphore<numSpins = 1000>");
    if (numThreads > 1) {
        testSemaphoresImpl<TrySemaphore<1000>>(numThreads, "TrySemaphore<numSpins = 1000>");
        testSemaphoresImpl<Padded<TrySemaphore<1000>>>(numThreads,
                                                       "Padded<TrySemaphore<numSpins = 1000>>");
    }
    printf("-----\n");

    testSemaphoresImpl<TrySemaphore<10000>>(1, "TrySemaphore<numSpins = 10000>");
    if (numThreads > 1) {
        testSemaphoresImpl<TrySemaphore<10000>>(numThreads, "TrySemaphore<numSpins = 10000>");
        testSemaphoresImpl<Padded<TrySemaphore<10000>>>(
            numThreads, "Padded<TrySemaphore<numSpins = 10000>>");
    }
    printf("-----\n");

    testSemaphoresImpl<Benaphore<0>>(1, "Benaphore<no spin>");
    if (numThreads > 1) {
        testSemaphoresImpl<Benaphore<0>>(numThreads, "Benaphore<no spin>");
        testSemaphoresImpl<Padded<Benaphore<0>>>(numThreads,
                                                     "Padded<Benaphore<no spin>>");
    }
    printf("-----\n");

    testSemaphoresImpl<Benaphore<1000>>(1, "Benaphore<numSpins = 1000>");
    if (numThreads > 1) {
        testSemaphoresImpl<Benaphore<1000>>(numThreads, "Benaphore<numSpins = 1000>");
        testSemaphoresImpl<Padded<Benaphore<1000>>>(numThreads,
                                                     "Padded<Benaphore<numSpins = 1000>>");
    }
    printf("-----\n");

    testSemaphoresImpl<Benaphore<10000>>(1, "Benaphore<numSpins = 10000>");
    if (numThreads > 1) {
        testSemaphoresImpl<Benaphore<10000>>(numThreads, "Benaphore<numSpins = 10000>");
        testSemaphoresImpl<Padded<Benaphore<10000>>>(numThreads,
                                                     "Padded<Benaphore<numSpins = 10000>>");
    }
    printf("-----\n");

    testSemaphoresImpl<CondVarSemaphore<true>>(1, "std::condition_variable<notifyWithLock = true>");
    if (numThreads > 1) {
        testSemaphoresImpl<CondVarSemaphore<true>>(
            numThreads, "std::condition_variable<notifyWithLock = true>");
        testSemaphoresImpl<Padded<CondVarSemaphore<true>>>(
            numThreads, "Padded<std::condition_variable<notifyWithLock = true>>");
    }
    printf("-----\n");

    testSemaphoresImpl<CondVarSemaphore<false>>(1,
                                                "std::condition_variable<notifyWithLock = false>");
    if (numThreads > 1) {
        testSemaphoresImpl<CondVarSemaphore<false>>(
            numThreads, "std::condition_variable<notifyWithLock = false>");
        testSemaphoresImpl<Padded<CondVarSemaphore<false>>>(
            numThreads, "Padded<std::condition_variable<notifyWithLock = false>>");
    }
    printf("-----\n");

    testSemaphoresImpl<SpinSemaphore<1000>>(1, "SpinSemaphore<with backoff spins = 1000>");
    if (numThreads > 1) {
        testSemaphoresImpl<SpinSemaphore<1000>>(numThreads,
                                                "SpinSemaphore<with backoff spins = 1000>");
        testSemaphoresImpl<Padded<SpinSemaphore<1000>>>(
            numThreads, "Padded<SpinSemaphore<with backoff spins = 1000>>");
    }
    printf("-----\n");

    testSemaphoresImpl<SpinSemaphore<10000>>(1, "SpinSemaphore<backoff spins = 10000>");
    if (numThreads > 1) {
        testSemaphoresImpl<SpinSemaphore<10000>>(numThreads,
                                                 "SpinSemaphore<backoff spins = 10000>");
        testSemaphoresImpl<Padded<SpinSemaphore<10000>>>(
            numThreads, "Padded<SpinSemaphore<backoff spins = 10000>>");
    }
    printf("-----\n");

    testSemaphoresImpl<SpinSemaphore<0>>(1, "SpinSemaphore<with no backoff spins>");
    if (numThreads > 1) {
        testSemaphoresImpl<SpinSemaphore<0>>(numThreads, "SpinSemaphore<with no backoff>");
        testSemaphoresImpl<Padded<SpinSemaphore<0>>>(numThreads,
                                                     "Padded<SpinSemaphore<with no backoff>>");
    }
}
