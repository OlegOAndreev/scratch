#include "fiber.h"

#include <atomic>

#include "common.h"

#if defined(__APPLE__) || defined(__linux__)

// setjmp/longjmp implementation, taken from libco/sjlj.c. This implementation is a real horror movie.

#include <csignal>
// At least on gcc+glibc the longjmp does the unneeded checks (see e.g. https://bugzilla.redhat.com/show_bug.cgi?id=557316)
// Disable those checks
#pragma push_macro("_FORTIFY_SOURCE")
#pragma push_macro("__USE_FORTIFY_LEVEL")
#undef _FORTIFY_SOURCE
#undef __USE_FORTIFY_LEVEL
#include <csetjmp>
#pragma pop_macro("_FORTIFY_SOURCE")
#pragma pop_macro("__USE_FORTIFY_LEVEL")
#include <mutex>

struct FiberId::FiberImpl {
    char* stack = nullptr;
    sigjmp_buf context = {};

    // The following two fields are used only when first starting the fiber.
    void(*entry)(void*) = nullptr;
    void* arg = nullptr;
};

namespace {

static int const kLongJmpVal = 123;

// The only way to pass arguments into sighandler.
thread_local FiberId::FiberImpl* passToSignalImpl = nullptr;
// Currently running fiber.
thread_local FiberId::FiberImpl* runningImpl = nullptr;
// Place in the running thread, which calls the first switchTo(). Invariant: if runningImpl is not null, this
// context contains parent thread context.
thread_local sigjmp_buf parentThreadContext = {};

void fiberEntry(int)
{
    // Copy thread-local var to stack, otherwise the second call to create() will overwrite the first value.
    FiberId::FiberImpl* impl = passToSignalImpl;
    if (sigsetjmp(impl->context, 0) == kLongJmpVal) {
        try {
            impl->entry(impl->arg);
        } catch (...) {
            fprintf(stderr, "Uncaught exception in fiber entry, aborting\n");
            abort();
        }
        // Return to the point in the parent thread which called switchTo().
        ENSURE(runningImpl == impl, "Inconsistent fiber state");
        runningImpl = nullptr;
        siglongjmp(parentThreadContext, kLongJmpVal);
    }
}

} // namespace

FiberId FiberId::create(size_t stackSize, void (*entry)(void*), void* arg)
{
    // MINSIGSTKSZ is 32kB on MacOS, 2kB on glibc/musl x86-64, make it double just in case.
    if (stackSize < MINSIGSTKSZ * 2) {
        stackSize = MINSIGSTKSZ * 2;
    }
    FiberImpl* impl = new FiberImpl;
    impl->stack = new char[stackSize];
    impl->entry = entry;
    impl->arg = arg;

    // Signal manipulations (sigaction, sigaltstack) are process-wide, so make the whole operation thread-safe
    // with a big mutex.
    {
        static std::mutex signalMutex;
        std::unique_lock<std::mutex> signalLock(signalMutex);

        stack_t sigstack;
        sigstack.ss_sp = impl->stack;
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

        passToSignalImpl = impl;
        if (raise(SIGUSR2) < 0) {
            FAIL("raise");
        }
        passToSignalImpl = nullptr;

        if (oldsigstack.ss_size > 0) {
            if (sigaltstack(&oldsigstack, nullptr) < 0) {
                FAIL("sigaltstack old");
            }
        }
        if (sigaction(SIGUSR2, &oldsigact, nullptr) < 0) {
            FAIL("sigaction old");
        }
    }

    FiberId ret;
    ret.impl = impl;
    return ret;
}

void FiberId::switchTo()
{
    if (runningImpl == impl) {
        return;
    }
    if (runningImpl != nullptr) {
        if (sigsetjmp(runningImpl->context, 0) == kLongJmpVal) {
            // Currently running fiber has been switched back to.
            return;
        }
    } else {
        // We are not in the fiber right now, store the thread context to return to.
        if (sigsetjmp(parentThreadContext, 0) == kLongJmpVal) {
            return;
        }
    }
    runningImpl = impl;
    siglongjmp(impl->context, kLongJmpVal);
}

void FiberId::destroy()
{
    delete[] impl->stack;
    delete impl;
}

#elif defined(_WIN32)

// Windows fiber implementation, taken from libco/fiber.c

#include <windows.h>

struct FiberId::FiberImpl {
    void* handle = nullptr;

    // The following two fields are used only when first starting the fiber.
    void(*entry)(void*) = nullptr;
    void* arg = nullptr;
};
};

namespace {

// Must be set to the return value of ConvertThreadToFiber, used when returning from fiber entry back to "main"
// thread entry.
thread_local void* mainThreadFiber = nullptr;
// Currently running fiber.
thread_local FiberId::FiberImpl* runningImpl = nullptr;

void __stdcall fiberEntry(void* arg)
{
    FiberId::FiberImpl* impl = (FiberId::FiberImpl*)arg;
    try {
        impl->entry(impl->arg);
    } catch (...) {
        fprintf(stderr, "Uncaught exception in fiber entry, aborting\n");
        abort();
    }
    // Return control the point in the original thread which called switchTo() first.
    runningImpl = nullptr;
    SwitchToFiber(mainThreadFiber);
}

} // namespace

FiberId FiberId::create(size_t stackSize, void (*entry)(void*), void* arg)
{
    if (mainThreadFiber == nullptr) {
        mainThreadFiber = ConvertThreadToFiber(0);
    }
    FiberImpl* impl = new FiberImpl;
    impl->fiberHandle = CreateFiber(stackSize, fiberEntry, (void*)impl);
    impl->entry = entry;
    impl->arg = arg;

    FiberId ret;
    ret.impl = impl;
    return ret;
}

void FiberId::switchTo()
{
    if (mainThreadFiber == nullptr) {
        mainThreadFiber = ConvertThreadToFiber(0);
    }
    if (runningImpl == impl) {
        return;
    }
    runningImpl = impl;
    SwitchToFiber(impl->fiberHandle);
}

void FiberId::destroy()
{
    DestroyFiber(impl->fiberHandle);
    delete impl;
}

#else

#error "Unsupported OS"

#endif

