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

struct MarlFiber::Handle {
    char* stack = nullptr;
    marl_fiber_context context = {};

    // The following two fields are used only when first starting the fiber.
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
};

namespace {

// Used when returning from fiber entry back to "main" thread entry.
thread_local marl_fiber_context tlMainThreadContext = {};

void fiberEntry(void* arg)
{
    MarlFiber::Handle* handle = (MarlFiber::Handle*)arg;
    try {
        handle->entry(handle->arg);
    } catch (...) {
        fprintf(stderr, "Uncaught exception in fiber entry, aborting\n");
        abort();
    }
    marl_fiber_swap(&handle->context, &tlMainThreadContext);
}

} // namespace

MarlFiber::Handle* MarlFiber::create(size_t stackSize, void (*entry)(void*), void* arg)
{
    MarlFiber::Handle* handle = new MarlFiber::Handle;
    handle->stack = new char[stackSize];
    handle->entry = entry;
    handle->arg = arg;
    marl_fiber_set_target(&handle->context, handle->stack, stackSize, fiberEntry, handle);
    return handle;
}

void MarlFiber::switchTo(Handle* from, Handle* to)
{
    marl_fiber_swap(&from->context, &to->context);
}

void MarlFiber::switchFromThread(Handle* to)
{
    marl_fiber_swap(&tlMainThreadContext, &to->context);
}

void MarlFiber::destroy(Handle* handle)
{
    delete[] handle->stack;
    delete handle;
}
