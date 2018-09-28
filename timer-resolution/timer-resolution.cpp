#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#elif defined(__linux__)
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#else
#error "Unsupported OS"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <signal.h>
#include <unwind.h>
#include <sys/select.h>
#include <sys/time.h>

int dummy = 0;

#if 0
void getStackTrace(void** stack, ucontext_t* ucontext)
{
    unw_cursor_t cursor;
    unw_init_local_safe(&cursor, ucontext);

    size_t pos = 0;
    while (pos < MAX_STACK_SIZE) {
        if (unw_step(&cursor) == 0) {
            break;
        }
        unw_word_t pc;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (pc == 0) {
            break;
        }
        stack[pos] = (void*)pc;
        pos++;
    }
}
#endif

uint64_t getThreadId()
{
#if defined(__APPLE__)
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#elif defined(__linux__)
    return syscall(SYS_gettid);
#endif
}

// A very primitive open addressing map.
struct SigCountMap {
    static const size_t MAX_THREAD_IDS = 99991;

    uint64_t threadIds[MAX_THREAD_IDS];
    int counts[MAX_THREAD_IDS];

    void clear()
    {
        for (size_t i = 0; i < MAX_THREAD_IDS; i++) {
            threadIds[i] = UINT64_MAX;
            counts[i] = 0;
        }
    }

    void inc(uint64_t threadId)
    {
        size_t pos = (size_t)(threadId % MAX_THREAD_IDS);
        while (threadIds[pos] != threadId && threadIds[pos] != UINT64_MAX) {
            pos = (pos + 1) % MAX_THREAD_IDS;
        }
        threadIds[pos] = threadId;
        counts[pos]++;
    }
};

SigCountMap sigCount;

int sleepCount = 0;
uint64_t sleepTime = 0;

void simpleSigHandler(int /*sig*/, siginfo_t* /*sinfo*/, void* /*ucontext*/)
{
    sigCount.inc(getThreadId());
}

struct StackTrace {
    static const size_t MAX_STACK_DEPTH = 256;
    void* stack[MAX_STACK_DEPTH];
    size_t count = 0;
};

_Unwind_Reason_Code libgccUnwindOneFrame(struct _Unwind_Context* uc, void* arg)
{
    StackTrace* trace = (StackTrace*)arg;
    trace->stack[trace->count] = (void*)_Unwind_GetIP(uc);
    trace->count++;
    if (trace->count == StackTrace::MAX_STACK_DEPTH) {
        return _URC_END_OF_STACK;
    }
    return _URC_NO_REASON;
}

void libgccUnwindSigHandler(int /*sig*/, siginfo_t* /*sinfo*/, void* /*ucontext*/)
{
    sigCount.inc(getThreadId());
    StackTrace trace;
    _Unwind_Backtrace(libgccUnwindOneFrame, &trace);
    for (size_t i = 0; i < trace.count; i++) {
        dummy ^= (int)(intptr_t)trace.stack[i];
    }
}

using SigHandlerFunction = void(*)(int, siginfo_t*, void*);

void startItimerHandler(int which, int frequency, SigHandlerFunction handler)
{
    int signo;
    switch (which) {
        case ITIMER_REAL:
            signo = SIGALRM;
            break;
        case ITIMER_VIRTUAL:
            signo = SIGVTALRM;
            break;
        case ITIMER_PROF:
            signo = SIGPROF;
            break;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, nullptr) != 0) {
        printf("Failed to install signal handler\n");
        exit(1);
    }

    struct itimerval ival;
    ival.it_interval.tv_sec = 0;
    ival.it_interval.tv_usec = 1000000 / frequency;
    ival.it_value = ival.it_interval;
    if (setitimer(which, &ival, nullptr) != 0) {
        printf("Failed to set timer\n");
        exit(1);
    }
}

void stopItimer(int which)
{
    struct itimerval ival;
    ival.it_interval.tv_sec = 0;
    ival.it_interval.tv_usec = 0;
    ival.it_value = ival.it_interval;
    setitimer(which, &ival, nullptr);
}

#if defined(__linux__)
timer_t timerId;

void startPosixTimerHandler(clockid_t clockId, int frequency, SigHandlerFunction handler)
{
    int signo = SIGPROF;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, nullptr) != 0) {
        printf("Failed to install signal handler\n");
        exit(1);
    }

    struct sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = signo;
    if (timer_create(clockId, &sev, &timerId) != 0) {
        printf("Failed to install signal handler\n");
        exit(1);
    }

    struct itimerspec tspec;
    tspec.it_interval.tv_sec = 0;
    tspec.it_interval.tv_nsec = 1000000000 / frequency;
    tspec.it_value = tspec.it_interval;
    if (timer_settime(timerId, 0, &tspec, nullptr) != 0) {
        printf("Failed to set timer\n");
        exit(1);
    }
}

void stopPosixTimer()
{
    timer_delete(timerId);
}
#endif

void dummyCircularMove(char* dst, const char* src, size_t size, size_t stride)
{
    memmove(dst, src + stride, size - stride);
    memmove(dst + size - stride, src, stride);
}

void dummyWork()
{
    char src[100000];
    char dst[100000];
    for (size_t i = 0; i < sizeof(src); i++) {
        src[i] = (i * i) & 0xFF;
    }

    for (size_t i = 0; i < sizeof(src); i++) {
        dummyCircularMove(dst, src, sizeof(src), i);
        if (i % 100 == 0) {
            struct timespec ts, remts;
            ts.tv_sec = 0;
            ts.tv_nsec = 10000; // 1/100th of msec
            remts.tv_sec = 0;
            remts.tv_nsec = 0;
            nanosleep(&ts, &remts);
            sleepCount++;
            sleepTime += ts.tv_nsec - remts.tv_nsec;
        }
    }
    dummy += dst[0];
}

void* dummyWorkHelper(void*)
{
    dummyWork();
    return nullptr;
}

// Run dummyWork in two threads.
void threadedDummyWork()
{
    pthread_t childThread;
    if (pthread_create(&childThread, nullptr, dummyWorkHelper, nullptr) != 0) {
        printf("Failed to create thread\n");
        abort();
    }
    dummyWork();
    pthread_join(childThread, nullptr);
}

uint64_t clockGetNsec(clockid_t clockId)
{
    struct timespec ts;
    clock_gettime(clockId, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

uint64_t clockGetMsec(clockid_t clockId)
{
    return clockGetNsec(clockId) / 1000000;
}

void tryItimer(int which, int frequency, SigHandlerFunction handler)
{
    uint64_t monotonicStart = clockGetMsec(CLOCK_MONOTONIC);
    uint64_t realStart = clockGetMsec(CLOCK_REALTIME);
    uint64_t processCpuStart = clockGetMsec(CLOCK_PROCESS_CPUTIME_ID);

    sigCount.clear();
    sleepCount = 0;
    startItimerHandler(which, frequency, handler);
    threadedDummyWork();
    stopItimer(which);

    uint64_t monotonicDelta = clockGetMsec(CLOCK_MONOTONIC) - monotonicStart;
    uint64_t realDelta = clockGetMsec(CLOCK_REALTIME) - realStart;
    uint64_t processCpuDelta = clockGetMsec(CLOCK_PROCESS_CPUTIME_ID) - processCpuStart;

    int totalCount = 0;
    for (size_t i = 0; i < SigCountMap::MAX_THREAD_IDS; i++) {
        if (sigCount.threadIds[i] != UINT64_MAX) {
            printf("Thread %llu: %d sighandlers\n", (unsigned long long)sigCount.threadIds[i],
                   sigCount.counts[i]);
            totalCount += sigCount.counts[i];
        }
    }
    int realFrequency = totalCount * 1000 / monotonicDelta;

    printf("Frequency: %d (vs %d requested), times (msec): monotonic %d, real %d, process cpu %d,"
           " slept %d (%d times)\n",
           realFrequency, frequency,
           (int)monotonicDelta, (int)realDelta, (int)processCpuDelta,
           (int)(sleepTime / 1000000), sleepCount / 10);
}

#if defined(__linux__)
void tryPosixTimer(clockid_t clockId, int frequency, SigHandlerFunction handler)
{
    uint64_t monotonicStart = clockGetMsec(CLOCK_MONOTONIC);
    uint64_t realStart = clockGetMsec(CLOCK_REALTIME);
    uint64_t processCpuStart = clockGetMsec(CLOCK_PROCESS_CPUTIME_ID);

    sigCount.clear();
    sleepCount = 0;
    startPosixTimerHandler(clockId, frequency, handler);
    threadedDummyWork();
    stopPosixTimer();

    uint64_t monotonicDelta = clockGetMsec(CLOCK_MONOTONIC) - monotonicStart;
    uint64_t realDelta = clockGetMsec(CLOCK_REALTIME) - realStart;
    uint64_t processCpuDelta = clockGetMsec(CLOCK_PROCESS_CPUTIME_ID) - processCpuStart;

    int totalCount = 0;
    for (size_t i = 0; i < SigCountMap::MAX_THREAD_IDS; i++) {
        if (sigCount.threadIds[i] != UINT64_MAX) {
            printf("Thread %llu: %d sighandlers\n", (unsigned long long)sigCount.threadIds[i],
                   sigCount.counts[i]);
            totalCount += sigCount.counts[i];
        }
    }
    int realFrequency = totalCount * 1000 / monotonicDelta;

    printf("Frequency: %d (vs requested %d), times (msec): monotonic %d, real %d, process cpu %d, "
           "slept %d (%d times)\n",
           realFrequency, frequency,
           (int)monotonicDelta, (int)realDelta, (int)processCpuDelta,
           (int)(sleepTime / 1000000), sleepCount / 10);
}
#endif

void testClockResolution(clockid_t clockId)
{
    static const size_t NUM_ITERATIONS = 1000000;
    int deltas[NUM_ITERATIONS];
    size_t numDeltas = 0;
    uint64_t start = clockGetNsec(clockId);
    uint64_t prev = start;
    uint64_t dudummy = 0;
    for (size_t i = 0; i < NUM_ITERATIONS; i++) {
        uint64_t current = clockGetNsec(clockId);
        if (current != prev) {
            deltas[numDeltas] = (int)(current - prev);
            numDeltas++;
            prev = current;
        }
        dudummy += current;
    }
    uint64_t end = clockGetNsec(clockId);
    std::sort(deltas, deltas + numDeltas);
    printf("Got %d non-zero timing (out of %d), p0 %d, p50 %d, p95 %d, p99 %d, p100 %d nsec\n",
            (int)numDeltas, (int)NUM_ITERATIONS, deltas[0], deltas[numDeltas / 2],
            deltas[numDeltas * 95 / 100], deltas[numDeltas * 99 / 100], deltas[numDeltas - 1]);
    printf("Average call cost: %d nsec\n", (int)((end - start) / NUM_ITERATIONS));
    dummy += (int)dudummy;
}

void spinningSleep(int nsec)
{
    uint64_t end = clockGetNsec(CLOCK_MONOTONIC) + nsec;
    while (clockGetNsec(CLOCK_MONOTONIC) < end) {
    }
}

template <typename SleepF>
void testSleepAccuracy(SleepF sleepf)
{
    const int nsecs[] = { 10000, 1000, 100, 1 };

    static const size_t NUM_ITERATIONS = 10000;
    int* deltas = new int[NUM_ITERATIONS];
    for (int nsec : nsecs) {
        uint64_t start = clockGetNsec(CLOCK_MONOTONIC);
        for (size_t i = 0; i < NUM_ITERATIONS; i++) {
            sleepf(nsec);
        }
        uint64_t end = clockGetNsec(CLOCK_MONOTONIC);

        for (size_t i = 0; i < NUM_ITERATIONS; i++) {
            uint64_t startIter = clockGetNsec(CLOCK_MONOTONIC);
            sleepf(nsec);
            deltas[i] = clockGetNsec(CLOCK_MONOTONIC) - startIter;
        }
        std::sort(deltas, deltas + NUM_ITERATIONS);
        printf("Slept %d instead of %d nsec, p0 %d, p50 %d, p95 %d, p99 %d, p100 %d nsec\n",
               (int)((end - start) / NUM_ITERATIONS), nsec, deltas[0], deltas[NUM_ITERATIONS / 2],
                deltas[NUM_ITERATIONS * 95 / 100], deltas[NUM_ITERATIONS * 99 / 100],
                deltas[NUM_ITERATIONS - 1]);
    }
    delete[] deltas;
}

int doSleepAccuracy = 1 << 0;
int doClockResolution = 1 << 1;
int doSignalResolution = 1 << 2;

void doMain(int doMask)
{
    if (doMask & doSleepAccuracy) {
        printf("\nTesting sleep accuracy\n");

        printf("Testing nanosleep sleep accuracy\n");
        testSleepAccuracy([](int nsec) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = nsec;
            nanosleep(&ts, nullptr);
        });

        printf("Testing pselect sleep accuracy\n");
        testSleepAccuracy([](int nsec) {
            struct timespec tv;
            tv.tv_sec = 0;
            tv.tv_nsec = nsec;
            pselect(0, nullptr, nullptr, nullptr, &tv, nullptr);
        });

        printf("Testing with pthread cond wait\n");
        pthread_mutex_t pthreadMutex;
        pthread_cond_t pthreadCond;
        pthread_mutex_init(&pthreadMutex, nullptr);
        pthread_cond_init(&pthreadCond, nullptr);
        testSleepAccuracy([&](int nsec) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += nsec;
            pthread_mutex_lock(&pthreadMutex);
            pthread_cond_timedwait(&pthreadCond, &pthreadMutex, &ts);
            pthread_mutex_unlock(&pthreadMutex);
        });


        printf("Testing spinning sleep accuracy\n");
        testSleepAccuracy([](int nsec) {
            spinningSleep(nsec);
        });

#if defined(__APPLE__)
        printf("Testing mach_wait_until sleep accuracy\n");
        testSleepAccuracy([](int nsec) {
            mach_timebase_info_data_t timebase_info;
            mach_timebase_info(&timebase_info);
            uint64_t time_to_wait = nsec * timebase_info.numer  / timebase_info.denom;
            uint64_t now = mach_absolute_time();
            mach_wait_until(now + time_to_wait);
        });
#endif

#if defined(__linux__)
        printf("Testing CLOCK_MONOTONIC relative clock_nanosleep sleep accuracy\n");
        testSleepAccuracy([](int nsec) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = nsec;
            clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
        });

        printf("Testing CLOCK_MONOTONIC absolute clock_nanosleep sleep accuracy\n");
        testSleepAccuracy([](int nsec) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_nsec += nsec;
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
        });

        int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (timerfd == -1) {
            printf("Failed to create timerfd\n");
            exit(1);
        }

        printf("Testing CLOCK_MONOTONIC timerfd sleep accuracy\n");
        testSleepAccuracy([=](int nsec) {
            itimerspec itspec;
            itspec.it_interval.tv_sec = 0;
            itspec.it_interval.tv_nsec = nsec;
            itspec.it_value = itspec.it_interval;
            if (timerfd_settime(timerfd, 0, &itspec, nullptr) == -1) {
                printf("Failed to set timerfd\n");
                exit(1);
            }
            uint64_t buf;
            if (read(timerfd, &buf, sizeof(buf)) == -1) {
                printf("Failed to read timerfd\n");
                exit(1);
            }
        });
        close(timerfd);
#endif
    }

    if (doMask & doClockResolution) {
        printf("\nTesting clock resolutions\n");

        printf("Testing CLOCK_MONOTONIC resolution\n");
        testClockResolution(CLOCK_MONOTONIC);
        printf("Testing CLOCK_PROCESS_CPUTIME_ID resolution\n");
        testClockResolution(CLOCK_PROCESS_CPUTIME_ID);
    }

    if (doMask & doSignalResolution) {
        printf("\nTesting profiling signal resolutions\n");

        int freqs[] = { 10, 50, 100, 300, 900, 1000, 2000, 10000 };
        printf("Testing ITIMER_PROF with libgcc unwind handler\n");
        for (int freq : freqs) {
            tryItimer(ITIMER_PROF, freq, libgccUnwindSigHandler);
        }
        printf("Testing ITIMER_PROF with simple handler\n");
        for (int freq : freqs) {
            tryItimer(ITIMER_PROF, freq, simpleSigHandler);
        }
        printf("Testing ITIMER_REAL with simple handler\n");
        for (int freq : freqs) {
            tryItimer(ITIMER_REAL, freq, simpleSigHandler);
        }
        printf("Testing ITIMER_VIRTUAL with simple handler\n");
        for (int freq : freqs) {
            tryItimer(ITIMER_VIRTUAL, freq, simpleSigHandler);
        }
#if defined(__linux__)
        printf("Testing CLOCK_MONOTONIC with simple handler\n");
        for (int freq : freqs) {
            tryPosixTimer(CLOCK_MONOTONIC, freq, simpleSigHandler);
        }
        printf("Testing CLOCK_PROCESS_CPUTIME_ID with simple handler\n");
        for (int freq : freqs) {
            tryPosixTimer(CLOCK_PROCESS_CPUTIME_ID, freq, simpleSigHandler);
        }
#endif
    }
}

int main(int argc, char** argv)
{
    int doMask;
    if (argc > 2) {
        printf("Usage: %s [sleep|clock|signal]\n", argv[0]);
        return 1;
    } else if (argc == 2) {
        if (strcmp(argv[1], "sleep") == 0) {
            doMask = doSleepAccuracy;
        } else if (strcmp(argv[1], "clock") == 0) {
            doMask = doClockResolution;
        } else if (strcmp(argv[1], "signal") == 0) {
            doMask = doSignalResolution;
        } else {
            printf("Usage: %s [sleep|clock|signal]\n", argv[0]);
            return 1;
        }
    } else {
        doMask = doSleepAccuracy | doClockResolution | doSignalResolution;
    }

    doMain(doMask);
#if defined(__linux__)
    printf("\nRetrying with different timer slack\n");
    printf("Setting timer slack to 1 nsec, was %d nsec\n", prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0));
    prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);

    doMain(doMask);

    printf("\nRetrying with RR scheduler\n");
    sched_param sparam;
    memset(&sparam, 0, sizeof(sparam));
    if (sched_setscheduler(0, SCHED_RR, &sparam) == 0) {
        doMain(doMask);
    } else {
        printf("Failed to set scheduler to RR\n");
    }
#endif

    return dummy;
}
