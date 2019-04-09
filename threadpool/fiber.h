#pragma once

#include <cstddef>
#include <cstdint>

// A simple low-level fiber library, inspired by and borrowed code from libco
// (https://byuu.org/library/libco/)
// NOTE: The interface is very primitive and not intended to be used from the high-level code.
// Unlike std::thread and similar classes, FiberId is a non-owning class, destroy() must be called
// for the fiber to be destroyed.
class FiberId {
public:
    struct FiberImpl;

    // Creates a new fiber with the given stackSize and the given entry function. entry(arg)
    // will be called upon next switchToFiber for this fiber. When the entry method exits,
    // it returns control to the place in the running thread which made the first call
    // to the switchTo().
    static FiberId create(size_t stackSize, void(*entry)(void*), void* arg);

    // Switches current fiber to this fiber. Does nothing if the currently running fiber
    // is switched to.
    void switchTo();

    // Destroys this fiber. Destroying the currently running fiber is not defined.
    void destroy();

private:
    FiberImpl* impl = nullptr;
};
