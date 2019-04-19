#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>

#include "common.h"

// SizedPoolAlloc allocates objects of fixed size.
//  * Each allocated object is identified by a 32-bit handle (uint32_t), at most 2^32 - 1
//    objects can be allocated.
//  * Handle 0 is a nullptr equivalent.
class SizedPoolAlloc {
public:
    // Initializes a new SizedPoolAlloc with given object size.
    SizedPoolAlloc(size_t objectSize_);
    SizedPoolAlloc(SizedPoolAlloc const&) = delete;

    // Allocates a new object and returns its handle.
    uint32_t allocate();

    // Deallocates previously allocated object.
    void deallocate(uint32_t handle);

    // Returns the pointer to the object with given handle.
    void* at(uint32_t handle);

    // Returns the object size for this pool.
    size_t getObjectSize() const;

private:
    // Freelist "next" index is 32-bit, so the minimal required object size is 4 bytes.
    static size_t const kMinObjectSize = 4;
    size_t const requestedObjectSize;
    size_t const objectSize;

    // Backing store for object allocations is grouped in buckets. The buckets are sized
    // exponentially: backing store size in bucket #i is 2^i * objectSize.
    struct Bucket {
        // Base pointer to objects in the bucket.
        void* basePtr = nullptr;
        // Each bucket is split into the used part and the free space. Objects from the used part
        // are either allocated or are in the freelist.
        std::atomic<uint32_t> inUseCount{0};
        // Must be held when allocating the basePtr. This is a simpler alternative to optimistic
        // concurrency (each thread allocates and the loser thread then deallocates).
        std::mutex allocMutex;

        ~Bucket();
    };

    static size_t const kNumBuckets = 32;
    std::unique_ptr<Bucket[]> const buckets;

    // freeListTop is the freelist top handle tagged with aba counter.
    static int const kAbaCounterShift = 32;
    static uint64_t const kAbaCounterMask = 0xFFFFFFFF00000000;
    static uint64_t const kHandleMask = 0xFFFFFFFF;

    // Freelist is a lock-free stack.
    std::atomic<uint64_t> freeListTop{0};

    // Index of the last used bucket (must have non-empty free space).
    std::atomic<uint32_t> curBucketIndex{0};

    // Utilities for converting the object handle to/from bucket index + in-bucket offset.
    FORCE_INLINE static uint32_t handleToBucketIdx(uint32_t handle);
    FORCE_INLINE static uint32_t handleToBucketOffset(uint32_t handle, uint32_t bucketIdx);
    FORCE_INLINE static uint32_t bucketToHandle(uint32_t bucketIdx, uint32_t offset);

    // Utilities for getting handle from freelist top and updating the top.
    FORCE_INLINE static uint32_t topToHandle(uint64_t top);
    FORCE_INLINE static uint64_t updateTopHandle(uint64_t top, uint32_t newHandle);

    FORCE_INLINE bool tryPopTop(uint32_t* handle);
    FORCE_INLINE void pushTop(uint32_t handle);

    FORCE_INLINE bool tryGetFromBucket(uint32_t* handle);

    void allocateBucket(uint32_t bucketIdx);
};

inline SizedPoolAlloc::SizedPoolAlloc(size_t objectSize_)
    : requestedObjectSize(objectSize_)
    , objectSize(objectSize_ > kMinObjectSize
                 ? objectSize_
                 : kMinObjectSize)
    , buckets(new Bucket[32])
{
    allocateBucket(0);
}

inline uint32_t SizedPoolAlloc::allocate()
{
    while (true) {
        uint32_t ret;
        if (tryPopTop(&ret)) {
            return ret;
        }
        if (tryGetFromBucket(&ret)) {
            return ret;
        }
    }
}

inline void SizedPoolAlloc::deallocate(uint32_t handle)
{
    ENSURE(handle != 0, "Handle 0 is the nullptr");
    pushTop(handle);
}

inline void* SizedPoolAlloc::at(uint32_t handle)
{
    ENSURE(handle != 0, "Handle 0 is the nullptr");
    uint32_t bucketIdx = handleToBucketIdx(handle);
    uint32_t offset = handleToBucketOffset(handle, bucketIdx);
    return (char*)buckets[bucketIdx].basePtr + offset * objectSize;
}

size_t SizedPoolAlloc::getObjectSize() const
{
    return requestedObjectSize;
}

FORCE_INLINE uint32_t SizedPoolAlloc::handleToBucketIdx(uint32_t handle)
{
    return nextLog2(handle) - 1;
}

inline uint32_t SizedPoolAlloc::handleToBucketOffset(uint32_t handle, uint32_t bucketIdx)
{
    return handle - (1 << bucketIdx);
}

FORCE_INLINE uint32_t SizedPoolAlloc::bucketToHandle(uint32_t bucketIdx, uint32_t offset)
{
    return (1 << bucketIdx) + offset;
}

FORCE_INLINE uint32_t SizedPoolAlloc::topToHandle(uint64_t top)
{
    return top & kHandleMask;
}

FORCE_INLINE uint64_t SizedPoolAlloc::updateTopHandle(uint64_t top, uint32_t newHandle)
{
    uint32_t abaCounter = (top & kAbaCounterMask) >> kAbaCounterShift;
    abaCounter++;
    return newHandle | ((uint64_t)abaCounter << kAbaCounterShift);
}

inline bool SizedPoolAlloc::tryPopTop(uint32_t* handle)
{
    uint64_t top = freeListTop.load(std::memory_order_relaxed);
    uint32_t topHandle = topToHandle(top);
    if (topHandle == 0) {
        return false;
    }
    // Try to pop the next freelist item.
    uint32_t nextHandle = load_u32(at(topHandle));
    uint64_t nextTop = updateTopHandle(top, nextHandle);
    if (!freeListTop.compare_exchange_weak(top, nextTop, std::memory_order_seq_cst)) {
        // The pop failed, should retry the allocation.
        return false;
    }
    *handle = topHandle;
    return true;
}

inline void SizedPoolAlloc::pushTop(uint32_t handle)
{
    void* ptr = at(handle);
    uint64_t top = freeListTop.load(std::memory_order_relaxed);
    while (true) {
        uint32_t topHandle = topToHandle(top);
        store_u32(ptr, topHandle);
        uint64_t nextTop = updateTopHandle(top, handle);
        if (freeListTop.compare_exchange_weak(top, nextTop, std::memory_order_seq_cst)) {
            break;
        }
    }
}

inline bool SizedPoolAlloc::tryGetFromBucket(uint32_t* handle)
{
    // Check if there is unused space in the current bucket.
    uint32_t bucketIdx = curBucketIndex.load(std::memory_order_seq_cst);
    Bucket& curBucket = buckets[bucketIdx];
    uint32_t bucketSize = 1 << bucketIdx;
    uint32_t inUseCount = curBucket.inUseCount.fetch_add(1, std::memory_order_seq_cst);
    if (inUseCount < bucketSize) {
        // We added one more object to the  used items in the bucket.
        uint32_t bucketOffset = inUseCount;
        *handle = bucketToHandle(bucketIdx, bucketOffset);
        return true;
    }
    curBucket.inUseCount.fetch_sub(1, std::memory_order_seq_cst);

    // Current bucket is full, try allocating a new bucket and retry the allocation.
    uint32_t nextBucketIdx = bucketIdx + 1;
    ENSURE(nextBucketIdx < kNumBuckets, "The pool has been depleted");
    allocateBucket(nextBucketIdx);
    return false;
}

void SizedPoolAlloc::allocateBucket(uint32_t bucketIdx)
{
    Bucket& bucket = buckets[bucketIdx];
    std::lock_guard<std::mutex> lock(bucket.allocMutex);
    // Check that two threads did not race to allocate the same bucket.
    if (bucket.basePtr != nullptr) {
        return;
    }
    size_t allocSize = objectSize * (1 << bucketIdx);
    bucket.basePtr = operator new(allocSize);
    // curBucketIndex is updated only when the allocMutex is locked. Mutexes from different
    // buckets cannot be locked at the same time.
    uint32_t oldCurBucketIdx = curBucketIndex.exchange(bucketIdx, std::memory_order_seq_cst);
    ENSURE(bucketIdx == 0 || oldCurBucketIdx == bucketIdx - 1, "Incorrect curBucketIndex");
}

SizedPoolAlloc::Bucket::~Bucket()
{
    operator delete(basePtr);
}
