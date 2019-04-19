#include "common.h"

#include <cstring>
#include <thread>
#include <vector>

#include "sizedpoolalloc.h"

void fillValue(void* ptr, uint32_t v, size_t objectSize)
{
    memset(ptr, 0xFF, objectSize);
    // Fill the whole object and then specifically overwrite the first bytes with value i.
    store_u32(ptr, v);
}

uint64_t allocDealloc(SizedPoolAlloc& sizedPoolAlloc, std::vector<uint32_t>& allocated)
{
    uint64_t numIterations = allocated.size();
    uint64_t startTime = getTimeTicks();
    for (uint64_t i = 0; i < numIterations; i++) {
        allocated[i] = sizedPoolAlloc.allocate();
    }
    for (uint64_t i = 0; i < numIterations; i++) {
        sizedPoolAlloc.deallocate(allocated[i]);
    }
    return numIterations * getTimeFreq() / (getTimeTicks() - startTime);
}

uint64_t allocWriteDealloc(SizedPoolAlloc& sizedPoolAlloc, std::vector<uint32_t>& allocated,
                           int threadNum)
{
    uint64_t numIterations = allocated.size();
    size_t objectSize = sizedPoolAlloc.getObjectSize();
    uint64_t startTime = getTimeTicks();
    for (uint64_t i = 0; i < numIterations; i++) {
        allocated[i] = sizedPoolAlloc.allocate();
        fillValue(sizedPoolAlloc.at(allocated[i]), i * threadNum, objectSize);
    }
    for (uint64_t i = 0; i < numIterations; i++) {
        ENSURE(load_u32(sizedPoolAlloc.at(allocated[i])) == (uint32_t)i * threadNum,
               "Corrupted data");
        store_u32(sizedPoolAlloc.at(allocated[i]), 0xDEADBEEF);
        sizedPoolAlloc.deallocate(allocated[i]);
    }
    return numIterations * getTimeFreq() / (getTimeTicks() - startTime);
}

uint64_t randomAllocWriteDealloc(SizedPoolAlloc& sizedPoolAlloc, std::vector<uint32_t>& allocSlots,
                                 uint64_t numIterations, int startSlot,
                                 uint64_t* numAllocs_, uint64_t* numDeallocs_)
{
    size_t objectSize = sizedPoolAlloc.getObjectSize();

    size_t const kRandomMultiply = 12345;
    size_t numSlots = allocSlots.size();

    uint64_t startTime = getTimeTicks();
    int numAllocs = 0;
    int numDeallocs = 0;
    size_t randomSlot = startSlot;
    for (uint64_t i = 0; i < numIterations; i++) {
        randomSlot = (randomSlot * kRandomMultiply) % numSlots;

        uint32_t index = allocSlots[randomSlot];
        uint32_t newIndex = sizedPoolAlloc.allocate();
        fillValue(sizedPoolAlloc.at(newIndex), randomSlot, objectSize);
        numAllocs++;
        if (index != 0) {
            ENSURE(load_u32(sizedPoolAlloc.at(index)) == randomSlot, "Corrupted data");
            sizedPoolAlloc.deallocate(index);
            numDeallocs++;
        }
        allocSlots[randomSlot] = newIndex;
    }
    *numAllocs_ = numAllocs;
    *numDeallocs_ = numDeallocs;
    return (numAllocs + numDeallocs) * getTimeFreq() / (getTimeTicks() - startTime);
}

uint64_t randomAllocWriteDeallocAtomic(SizedPoolAlloc& sizedPoolAlloc,
                                       std::atomic<uint32_t>* allocSlots, int numSlots,
                                       uint64_t numIterations, int startSlot,
                                       uint64_t* numAllocs_, uint64_t* numDeallocs_)
{
    size_t objectSize = sizedPoolAlloc.getObjectSize();

    size_t const kRandomMultiply = 12345;

    uint64_t startTime = getTimeTicks();
    uint64_t numAllocs = 0;
    uint64_t numDeallocs = 0;
    size_t randomSlot = startSlot;
    for (uint64_t i = 0; i < numIterations; i++) {
        randomSlot = (randomSlot * kRandomMultiply) % numSlots;

        uint32_t newIndex = sizedPoolAlloc.allocate();
        fillValue(sizedPoolAlloc.at(newIndex), randomSlot, objectSize);
        numAllocs++;
        uint32_t index = allocSlots[randomSlot].exchange(newIndex);
        if (index != 0) {
            ENSURE(load_u32(sizedPoolAlloc.at(index)) == randomSlot, "Corrupted data");
            sizedPoolAlloc.deallocate(index);
            numDeallocs++;
        }
    }
    *numAllocs_ = numAllocs;
    *numDeallocs_ = numDeallocs;
    return (numAllocs + numDeallocs) * getTimeFreq() / (getTimeTicks() - startTime);
}

void testSizedPoolAllocImpl(size_t objectSize, int numThreads)
{
    printf("Testing SizedPoolAlloc(%d) with %d thread(s)\n", (int)objectSize, numThreads);
    printf("-----\n");

    {
        // Alloc-dealloc-alloc-dealloc again a bunch of nodes.
        int const kNumIterations = 10000000 / numThreads;
        SizedPoolAlloc sizedPoolAlloc(objectSize);

        std::vector<std::thread> threads;
        std::vector<uint64_t> allocPerSec1;
        std::vector<uint64_t> allocPerSec2;
        allocPerSec1.resize(numThreads);
        allocPerSec2.resize(numThreads);

        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back([&, i] {
                std::vector<uint32_t> allocated;
                allocated.resize(kNumIterations);

                allocPerSec1[i] = allocDealloc(sizedPoolAlloc, allocated);
                allocPerSec2[i] = allocDealloc(sizedPoolAlloc, allocated);
            });
        }
        for (int i = 0; i < numThreads; i++) {
            threads[i].join();
        }

        printf("%lld allocations+deallocations/sec followed by"
               " %lld allocations+deallocations/sec\n",
               (long long)simpleAverage(allocPerSec1), (long long)simpleAverage(allocPerSec2));

        printf("=====\n");
    }

    {
        // Alloc-dealloc-alloc-dealloc bunch of nodes, writing to allocated memory.
        uint64_t const kNumIterations = 10000000 / numThreads;
        SizedPoolAlloc sizedPoolAlloc(objectSize);

        std::vector<std::thread> threads;
        std::vector<uint64_t> allocPerSec1;
        std::vector<uint64_t> allocPerSec2;
        allocPerSec1.resize(numThreads);
        allocPerSec2.resize(numThreads);

        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back([&, i] {
                std::vector<uint32_t> allocated;
                allocated.resize(kNumIterations);

                allocPerSec1[i] = allocWriteDealloc(sizedPoolAlloc, allocated, i + 1);
                allocPerSec2[i] = allocWriteDealloc(sizedPoolAlloc, allocated, i + 1);
            });
        }
        for (int i = 0; i < numThreads; i++) {
            threads[i].join();
        }

        printf("%lld allocations+writes+deallocations/sec followed by"
               " %lld allocations+writes+deallocations/sec\n",
               (long long)simpleAverage(allocPerSec1), (long long)simpleAverage(allocPerSec2));

        printf("=====\n");
    }

    {
        // Randomly allocate-deallocate objects from fixed number of slots.
        size_t const kNumSlots = 1 << 20;
        uint64_t const kNumIterations = 10000000 / numThreads;
        SizedPoolAlloc sizedPoolAlloc(objectSize);

        std::vector<std::thread> threads;
        std::vector<uint64_t> opsPerSec;
        std::vector<uint64_t> numAllocs;
        std::vector<uint64_t> numDeallocs;
        opsPerSec.resize(numThreads);
        numAllocs.resize(numThreads);
        numDeallocs.resize(numThreads);

        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back([&, i] {
                std::vector<uint32_t> allocSlots;
                allocSlots.resize(kNumSlots, 0);

                opsPerSec[i] = randomAllocWriteDealloc(sizedPoolAlloc, allocSlots, kNumIterations,
                                                       i + 1, &numAllocs[i], &numDeallocs[i]);
            });
        }
        for (int i = 0; i < numThreads; i++) {
            threads[i].join();
        }

        printf("%lld random operations+writes/sec (%lld allocs, %lld deallocs)\n",
               (long long)simpleAverage(opsPerSec),
               (long long)simpleAverage(numAllocs), (long long)simpleAverage(numDeallocs));

        printf("=====\n");
    }

    if (numThreads > 1) {
        // Randomly allocate-deallocate objects from fixed number of slots, which is shared
        // among threads and is small (to have high thread contention). This tries to simulate
        // using the pool for the mpmc queue.
        size_t const kNumSlots = 1 << 6;
        uint64_t const kNumIterations = 10000000 / numThreads;
        SizedPoolAlloc sizedPoolAlloc(objectSize);

        std::vector<std::thread> threads;
        std::vector<uint64_t> opsPerSec;
        std::vector<uint64_t> numAllocs;
        std::vector<uint64_t> numDeallocs;
        opsPerSec.resize(numThreads);
        numAllocs.resize(numThreads);
        numDeallocs.resize(numThreads);

        std::unique_ptr<std::atomic<uint32_t>[]> allocSlots;
        allocSlots.reset(new std::atomic<uint32_t>[kNumSlots]);
        for (size_t i = 0; i < kNumSlots; i++) {
            allocSlots[i] = 0;
        }

        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back([&, i] {
                opsPerSec[i] = randomAllocWriteDeallocAtomic(sizedPoolAlloc, allocSlots.get(),
                                                             kNumSlots, kNumIterations, i + 1,
                                                             &numAllocs[i], &numDeallocs[i]);
            });
        }
        for (int i = 0; i < numThreads; i++) {
            threads[i].join();
        }

        printf("%lld random atomic operations+writes/sec (%lld allocs, %lld deallocs)\n",
               (long long)simpleAverage(opsPerSec),
               (long long)simpleAverage(numAllocs), (long long)simpleAverage(numDeallocs));

        printf("=====\n");
    }
}

void testSizedPoolAlloc(int numThreads)
{
    printf("Testing SizedPoolAlloc\n");

    testSizedPoolAllocImpl(4, 1);
    testSizedPoolAllocImpl(4, numThreads);
    testSizedPoolAllocImpl(64, 1);
    testSizedPoolAllocImpl(64, numThreads);
}
