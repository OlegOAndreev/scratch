#pragma once

#include <atomic>

#include "common.h"

// A synchronization primitive for waiting until the target count value is reached (similar to
// CountDownLatch in Java). CountWaiter is MPMC, both post() and wait() can be called by several
// threads concurrently.
class CountWaiter {
public:
    CountWaiter(int32_t targetCount);
    ~CountWaiter();

    // Posts (i.e. increases) the count value by the given amount. Returns true if at least one
    // waiter have been woken up by this post.
    bool post(int32_t count = 1);

    // Waits until the target count value has been hit.
    void wait();

    // Returns current count value.
    int32_t count() const;

private:
    // state consists of two separate int32 values: top value is the counter value, bottom is
    // the number of threads currently waiting for the counter.
    //
    // NOTE: All the operations are declared seq_cst because of the weak guarantees of C++ standard
    // for acq_rel operations, as discussed here:
    // https://stackoverflow.com/questions/52606524/what-exact-rules-in-the-c-memory-model-prevent-reordering-before-acquire-opera/
    //
    // NOTE: Sticking both counter value and number of waiting threads in one atomic variable
    // is complicated but neccessary to ensure correctness in the following use-case:
    //
    // producerThread(CountWaiter* cw) {
    //   cw->post(); <-- Here we need to guarantee that CountWaiter pointer does not become invalid
    //                   until the post() completes.
    // }
    // consumerThread() {
    //   CountWaiter cw(1);
    //   submitTask([&] { producerThread(&cw); });
    //   cw.wait();
    //  } <-- Here the CountWaiter gets destroyed upon exiting the scope.
    //
    // The solution to this problem is atomically updating both counter and number of waiters.
    // Note, that we trust that the following code is correct:
    //
    // producerThread(Semaphore* sem) {
    //   sem->post();
    // }
    // consumerThread() {
    //   Semaphore sem;
    //   submitTask([&] { producerThread(&sem); });
    //   sem.wait();
    //  }
    // I.e. the moment the Semaphore::post() calls the underlying OS primitive, it no longer
    // requires Semaphore object to be alive. This is definitely true for Linux (semaphores
    // are just memory locations), but is not guaranteed for MacOS/Windows AFAIK, so we simply
    // hope for the best on these OS. This particular issue could easily be solved by
    // allocating Semaphores from a Semaphore pool (see getOrAllocSemaphore).
    //
    // NOTE: In general this implementation looks a lot more complicated than it should be,
    // maybe we should replace this all with shared_ptr and passing by value?
    static int64_t const kStateNumWaitersMask = 0xFFFFFFFF;
    static size_t const kStateNumWaitersShift = 0;
    static int64_t const kStateCounterMask = 0xFFFFFFFF00000000LL;
    static size_t const kStateCounterShift = 32;

    std::atomic<int64_t> state;
    // semaphore gets allocated by the first thread trying to access it.
    std::atomic<Semaphore*> semaphorePtr{nullptr};

    // Get the semaphore or allocate it if it has not been allocated.
    Semaphore* getOrAllocSemaphore();

    static int32_t counterFromState(int64_t state);
    static int32_t numWaitersFromState(int64_t state);
    static int64_t stateFromCounter(int32_t counter);
    static int64_t stateFromNumWaiters(int32_t numWaiters);
};

CountWaiter::CountWaiter(int32_t targetCount)
    : state(stateFromCounter(targetCount))
{
}

CountWaiter::~CountWaiter()
{
    delete semaphorePtr.load(std::memory_order_seq_cst);
}

bool CountWaiter::post(int32_t count)
{
    int64_t stateDiff = stateFromCounter(count);
    int64_t oldState = state.fetch_sub(stateDiff, std::memory_order_seq_cst);
    int32_t oldCounter = counterFromState(oldState);
    if (oldCounter > count) {
        return false;
    }

    int32_t numWakeup = numWaitersFromState(oldState);
    if (numWakeup > 0) {
        Semaphore* semaphore = getOrAllocSemaphore();
        for (int i = 0; i < numWakeup; i++) {
            semaphore->post();
        }
    }
    return true;
}

void CountWaiter::wait()
{
    // Try to see if the target value has already been reached before trying to do any writes.
    int32_t counter = counterFromState(state.load(std::memory_order_seq_cst));
    if (counter <= 0) {
        return;
    }

    // Increment the number of waiters.
    int64_t stateDiff = numWaitersFromState(1);
    int64_t oldState = state.fetch_add(stateDiff, std::memory_order_seq_cst);
    counter = counterFromState(oldState);
    // post() has been called before we increased the number of waiters.
    if (counter <= 0) {
        state.fetch_sub(stateDiff, std::memory_order_seq_cst);
        return;
    }

    Semaphore* semaphore = getOrAllocSemaphore();
    // Always wait on the semaphore before checking the counter. See comments for state
    // for explanation of the race condition this is solving.
    semaphore->wait();

    // The Semaphores should not have spurious wakeups, but still do a sanity check.
    counter = counterFromState(state.load(std::memory_order_seq_cst));
    ENSURE(counter == 0, "Spurious wake on Semaphore");

    // This is not needed currently (with infinite waits), but let's leave it for timed wait
    // in future.
    state.fetch_sub(1, std::memory_order_seq_cst);
    return;
}

int32_t CountWaiter::count() const
{
    return counterFromState(state.load(std::memory_order_seq_cst));
}

Semaphore* CountWaiter::getOrAllocSemaphore()
{
    Semaphore* semaphore = semaphorePtr.load(std::memory_order_seq_cst);
    if (semaphore != nullptr) {
        return semaphore;
    }
    Semaphore* newSem = new Semaphore();
    Semaphore* expectedSem = nullptr;
    if (semaphorePtr.compare_exchange_strong(expectedSem, newSem, std::memory_order_seq_cst)) {
        return newSem;
    } else {
        // Another thread allocated the semaphore before us, use it instead.
        // NOTE: There is a thundering herd problem when two waiter start at ~same time
        // and allocate two semaphores. It can be solved in the following ways:
        //  1) have only the first waiter (count by numWaiters) allocate the semaphore
        //     (makes all the other waiters wait the first thread if it goes to sleep before
        //     storing the semaphorePtr);
        //  2) have a pool of pre-allocated semaphores (introduces complexity);
        //  3) allocate per-thread semaphore and store it in thread_local variable and add
        //     to the lock-free list of waiters semaphores (introduces complexity and troubles
        //     with deallocating thread_local);
        //  4) have any sort of lock for allocating semaphores, e.g. the first of the waiters
        //     allocate the semaphore (can block other threads if the waiter thread goes
        //     to sleep).
        delete newSem;
        return expectedSem;
    }
}

int32_t CountWaiter::counterFromState(int64_t state)
{
    return (state & kStateCounterMask) >> kStateCounterShift;
}

int32_t CountWaiter::numWaitersFromState(int64_t state)
{
    return (state & kStateNumWaitersMask) >> kStateNumWaitersShift;
}

int64_t CountWaiter::stateFromCounter(int32_t counter)
{
    return ((int64_t)counter) << kStateCounterShift;
}

int64_t CountWaiter::stateFromNumWaiters(int32_t numWaiters)
{
    return ((int64_t)numWaiters) << kStateNumWaitersShift;
}
