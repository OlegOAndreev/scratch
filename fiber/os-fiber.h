#pragma once

#include <cstddef>
#include <cstdint>

// A simple low-level fiber library, inspired by and borrowed code from libco
// (https://byuu.org/library/libco/)
struct OsFiber {
    struct Handle;

    // Creates a new fiber with the given stackSize and the given entry function. entry(arg) will be
    // called upon next switchTo for this fiber. When the entry method exits, it returns control to
    // the place in the running thread which made the first call to the switchToFromThread().
    static Handle* create(size_t stackSize, void (*entry)(void*), void* arg);

    // Switches from one fiber to another fiber.
    static void switchTo(Handle* from, Handle* to);

    // Switches from current thread to fiber (see create()).
    static void switchFromThread(Handle* to);

    // Destroys this fiber. Destroying the currently running fiber is not defined.
    static void destroy(Handle* handle);
};
