#include "marl-fiber.h"

#include <cstdio>
#include <cstdlib>

#if defined(__x86_64__)
#include "3rdparty/marl/osfiber_asm_x64.h"
#elif defined(__aarch64__)
#include "3rdparty/marl/osfiber_asm_aarch64.h"
#else
#error "Unsupported target"
#endif

extern "C" {
// Defined in .S files.
void marl_fiber_swap(marl_fiber_context* from, const marl_fiber_context* to);
// Defined in .c files.
void marl_fiber_set_target(struct marl_fiber_context* ctx,
                           void* stack,
                           uint32_t stack_size,
                           void (*target)(void*),
                           void* arg);
}

struct MarlFiber::FiberImpl {
    char* stack = nullptr;
    marl_fiber_context context = {};

    // The following two fields are used only when first starting the fiber.
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
};

namespace {

// Used when returning from fiber entry back to "main" thread entry.
thread_local MarlFiber::FiberImpl* tlMainThreadImpl = nullptr;
thread_local MarlFiber::FiberImpl* tlRunningImpl = nullptr;

void switchToImpl(MarlFiber::FiberImpl* impl)
{
    marl_fiber_context* fromContext = &tlRunningImpl->context;
    tlRunningImpl = impl;
    marl_fiber_swap(fromContext, &impl->context);
}

void fiberEntry(void* arg)
{
    MarlFiber::FiberImpl* impl = (MarlFiber::FiberImpl*)arg;
    try {
        impl->entry(impl->arg);
    } catch (...) {
        fprintf(stderr, "Uncaught exception in fiber entry, aborting\n");
        abort();
    }
    // Return control the point in the original thread which called switchTo() first.
    switchToImpl(tlMainThreadImpl);
}

} // namespace

MarlFiber MarlFiber::create(size_t stackSize, void (*entry)(void*), void* arg)
{
    MarlFiber ret;
    ret.impl = new MarlFiber::FiberImpl;
    ret.impl->stack = new char[stackSize];
    ret.impl->entry = entry;
    ret.impl->arg = arg;
    marl_fiber_set_target(&ret.impl->context, ret.impl->stack, stackSize, fiberEntry, ret.impl);
    return ret;
}

void MarlFiber::switchTo()
{
    bool isMainThread = false;
    if (tlMainThreadImpl == nullptr) {
        isMainThread = true;
        tlMainThreadImpl = new MarlFiber::FiberImpl;
        tlRunningImpl = tlMainThreadImpl;
    }
    switchToImpl(impl);
    if (isMainThread) {
        tlMainThreadImpl = nullptr;
        tlRunningImpl = nullptr;
    }
}

void MarlFiber::destroy()
{
    delete[] impl->stack;
    delete impl;
}
