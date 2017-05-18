#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <sys/time.h>
#elif defined(__linux__)
#include <time.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#error "Unsupported OS"
#endif

int64_t getTimeCounter()
{
#if defined(__APPLE__)
    timeval tp;
    gettimeofday(&tp, nullptr);
    return tp.tv_sec * 1000000ULL + tp.tv_usec;
#elif defined(__linux__)
    timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
#elif defined(_WIN32)
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
#else
#error "Unsupported OS"
#endif
}

int64_t getTimeFreq()
{
#if defined(__APPLE__)
    return 1000000;
#elif defined(__linux__)
    return 1000000000;
#elif defined(_WIN32)
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    return li.QuadPart;
#else
#error "Unsupported OS"
#endif
}

const size_t kNumThreads = 2;

void simpleAdder(size_t* value, size_t next)
{
    *value += next;
}

void atomicRelaxedAdder(std::atomic<size_t>* value, size_t next)
{
    value->fetch_add(next, std::memory_order_relaxed);
}

void atomicSeqCstAdder(std::atomic<size_t>* value, size_t next)
{
    value->fetch_add(next, std::memory_order_seq_cst);
}

template<typename Value, typename Adder>
void doSum(size_t times, Value* value, Adder adder)
{
    size_t v = 1;
    for (size_t i = 0; i < times; i++) {
        v *= 123;
        // Very simple pseudo-random code to throw off the optimizer.
        if (v % 10 == 1) {
            adder(value, 1);
        } else {
            adder(value, -1);
        }
    }
}

template<typename Value, typename Adder>
void doMain(size_t times, size_t numThreads, const char* name, Value* values, size_t valuesStride, Adder adder)
{
    std::vector<std::thread> threads;
    for (size_t i = 0; i < numThreads - 1; i++) {
        threads.emplace_back([=] {
            doSum(times, &values[(i + 1) * valuesStride], adder);
        });
    }

    int64_t timeStart = getTimeCounter();
    doSum(times, &values[0], adder);
    printf("%lld %s adds per second\n", (long long)(times * getTimeFreq() / (getTimeCounter() - timeStart)),
           name);

    for (std::thread& thread : threads) {
        thread.join();
    }
}

int main(int argc, char** argv)
{
    long long times;
    if (argc == 1) {
        times = 10000000LL;
    } else {
        times = atoll(argv[1]);
    }
    printf("Running add %lld times\n", times);

    size_t simpleValues[kNumThreads * 16];
    std::atomic<size_t> atomicValues[kNumThreads * 16];

    printf("Testing with 1 threads\n");
    doMain(times, 1, "simple", simpleValues, 1, simpleAdder);
    doMain(times, 1, "relaxed atomic", atomicValues, 1, atomicRelaxedAdder);
    doMain(times, 1, "seqcst atomic", atomicValues, 1, atomicSeqCstAdder);

    printf("Testing with %d threads (sequential values)\n", (int)kNumThreads);
    doMain(times, kNumThreads, "simple", simpleValues, 1, simpleAdder);
    doMain(times, kNumThreads, "relaxed atomic", atomicValues, 1, atomicRelaxedAdder);
    doMain(times, kNumThreads, "seqcst atomic", atomicValues, 1, atomicSeqCstAdder);

    printf("Testing with %d threads (one value)\n", (int)kNumThreads);
    doMain(times, kNumThreads, "simple", simpleValues, 0, simpleAdder);
    doMain(times, kNumThreads, "relaxed atomic", atomicValues, 0, atomicRelaxedAdder);
    doMain(times, kNumThreads, "seqcst atomic", atomicValues, 0, atomicSeqCstAdder);

    printf("Testing with %d threads (strided values)\n", (int)kNumThreads);
    doMain(times, kNumThreads, "simple", simpleValues, 16, simpleAdder);
    doMain(times, kNumThreads, "relaxed atomic", atomicValues, 16, atomicRelaxedAdder);
    doMain(times, kNumThreads, "seqcst atomic", atomicValues, 16, atomicSeqCstAdder);

    return simpleValues[0] + atomicValues[0];
}
