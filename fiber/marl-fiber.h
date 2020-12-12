#pragma once

#include <cstddef>
#include <cstdint>

// A simple wrapper around 3rdparty/marl assembly fiber switching. The interface matches the FiberId.
class MarlFiber {
public:
    struct FiberImpl;

    static MarlFiber create(size_t stackSize, void (*entry)(void*), void* arg);
    void switchTo();
    void destroy();

private:
    FiberImpl* impl = nullptr;
};
