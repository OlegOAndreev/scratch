#pragma once

#include <cstddef>
#include <cstdint>

// A simple wrapper around 3rdparty/marl assembly fiber switching. The interface matches the FiberId.
struct MarlFiber {
    struct Handle;

    static Handle* create(size_t stackSize, void (*entry)(void*), void* arg);
    static void switchTo(Handle* from, Handle* to);
    static void switchFromThread(Handle* to);
    static void destroy(Handle* handle);
};
