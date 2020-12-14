#include "os-fiber.h"

#include <atomic>

#include "common.h"

#if defined(__APPLE__) || defined(__linux__)

// setjmp/longjmp implementation, taken from libco/sjlj.c. This implementation is
// a real horror show.

#include <csignal>
#include <mutex>

// At least on gcc+glibc the longjmp does the unneeded checks (see e.g.
// https://bugzilla.redhat.com/show_bug.cgi?id=557316). Disable these checks.
#pragma push_macro("_FORTIFY_SOURCE")
#pragma push_macro("__USE_FORTIFY_LEVEL")
#undef _FORTIFY_SOURCE
#undef __USE_FORTIFY_LEVEL
#include <csetjmp>
#pragma pop_macro("_FORTIFY_SOURCE")
#pragma pop_macro("__USE_FORTIFY_LEVEL")

struct OsFiber::Handle {
    char* stack = nullptr;
    sigjmp_buf context = {};

    // The following two fields are used only when first starting the fiber.
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
};

namespace {

static int const kLongJmpVal = 123;

// The only way to pass arguments into sighandler.
thread_local OsFiber::Handle* tlPassToSignalHandle = nullptr;
// Place in the running thread, which calls the first switchTo().
thread_local sigjmp_buf tlParentThreadContext = {};

void fiberEntry(int)
{
    // Copy thread-local var to stack, otherwise the second call to create() will overwrite
    // the first value.
    OsFiber::Handle* handle = tlPassToSignalHandle;
    if (sigsetjmp(handle->context, 0) == kLongJmpVal) {
        try {
            handle->entry(handle->arg);
        } catch (...) {
            fprintf(stderr, "Uncaught exception in fiber entry, aborting\n");
            abort();
        }
        siglongjmp(tlParentThreadContext, kLongJmpVal);
    }
}

} // namespace

OsFiber::Handle* OsFiber::create(size_t stackSize, void (*entry)(void*), void* arg)
{
    // MINSIGSTKSZ is 32kB on MacOS, 2kB on glibc/musl x86-64, make it double just in case.
    if (stackSize < MINSIGSTKSZ * 2) {
        stackSize = MINSIGSTKSZ * 2;
    }
    Handle* handle = new Handle;
    handle->stack = new char[stackSize];
    handle->entry = entry;
    handle->arg = arg;

    // Signal manipulations (sigaction, sigaltstack) are process-wide, so make the whole operation
    // thread-safe with a big mutex.
    {
        static std::mutex signalMutex;
        std::unique_lock<std::mutex> signalLock(signalMutex);

        stack_t sigstack;
        sigstack.ss_sp = handle->stack;
        sigstack.ss_size = stackSize;
        sigstack.ss_flags = 0;
        stack_t oldsigstack;
        if (sigaltstack(&sigstack, &oldsigstack) < 0) {
            FAIL("sigaltstack");
        }

        struct sigaction sigact;
        struct sigaction oldsigact;

        sigact.sa_handler = fiberEntry;
        sigact.sa_flags = SA_ONSTACK;
        sigemptyset(&sigact.sa_mask);
        if (sigaction(SIGUSR2, &sigact, &oldsigact) < 0) {
            FAIL("sigaction");
        }

        tlPassToSignalHandle = handle;
        if (raise(SIGUSR2) < 0) {
            FAIL("raise");
        }
        tlPassToSignalHandle = nullptr;

        if (oldsigstack.ss_size > 0) {
            if (sigaltstack(&oldsigstack, nullptr) < 0) {
                FAIL("sigaltstack old");
            }
        }
        if (sigaction(SIGUSR2, &oldsigact, nullptr) < 0) {
            FAIL("sigaction old");
        }
    }

    return handle;
}

void OsFiber::switchTo(Handle* from, Handle* to)
{
    if (sigsetjmp(from->context, 0) == kLongJmpVal) {
        // Currently running fiber has been switched back to.
        return;
    }
    siglongjmp(to->context, kLongJmpVal);
}

void OsFiber::switchFromThread(Handle* to)
{
    // We are not in the fiber right now, store the thread context to return to.
    if (sigsetjmp(tlParentThreadContext, 0) == kLongJmpVal) {
        return;
    }
    siglongjmp(to->context, kLongJmpVal);
}

void OsFiber::destroy(Handle* handle)
{
    delete[] handle->stack;
    delete handle;
}

#elif defined(_WIN32)

// Windows fiber implementation, taken from libco/fiber.c

#include <windows.h>

struct OsFiber::Handle {
    void* fiberHandle = nullptr;

    // The following two fields are used only when first starting the fiber.
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
};

namespace {

// Must be set to the return value of ConvertThreadToFiber, used when returning from fiber entry
// back to "main" thread entry.
thread_local void* tlMainThreadFiber = nullptr;

void __stdcall fiberEntry(void* arg)
{
    OsFiber::Handle* handle = (OsFiber::Handle*)arg;
    try {
        handle->entry(handle->arg);
    } catch (...) {
        fprintf(stderr, "Uncaught exception in fiber entry, aborting\n");
        abort();
    }
    SwitchToFiber(tlMainThreadFiber);
}

} // namespace

FiberId OsFiber::create(size_t stackSize, void (*entry)(void*), void* arg)
{
    Handle* handle = new Handle;
    handle->fiberHandle = CreateFiber(stackSize, fiberEntry, (void*)handle);
    handle->entry = entry;
    handle->arg = arg;
    return handle;
}

void OsFiber::switchTo(Handle* /*from*/, Handle* to)
{
    SwitchToFiber(to->fiberHandle);
}

void OsFiber::switchFromThread(Handle* to)
{
    tlMainThreadFiber = ConvertThreadToFiber(0);
    SwitchToFiber(to->fiberHandle);
}

void OsFiber::destroy(Handle* handle)
{
    DestroyFiber(handle->fiberHandle);
    delete handle;
}

#else

#error "Unsupported OS"

#endif
