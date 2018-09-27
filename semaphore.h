#pragma once

// Semaphore: an OS semaphore class with two methods: post() and wait().

#if defined(__linux__)
#include <semaphore.h>

struct Semaphore
{
    sem_t sema;
    Semaphore() { sem_init(&sema, 0, 0); }
    Semaphore(unsigned value) { sem_init(&sema, 0, value); }
    ~Semaphore() { sem_destroy(&sema); }
    void post() { sem_post(&sema); }
    void wait() { sem_wait(&sema); }
};
#elif defined(__APPLE__)
#include <dispatch/dispatch.h>

struct Semaphore
{
    dispatch_semaphore_t sema;
    Semaphore() { sema = dispatch_semaphore_create(0); }
    Semaphore(unsigned value) { sema = dispatch_semaphore_create(value); }
    ~Semaphore() { dispatch_release(sema); }
    void post() { dispatch_semaphore_signal(sema); }
    void wait() { dispatch_semaphore_wait(sema, ~uint64_t(0)); }
};
#elif defined(_WIN32)
#include <windows.h>

struct Semaphore
{
    HANDLE sema;
    Semaphore() { sema = CreateSemaphore(NULL, 0, MAXLONG, NULL); }
    Semaphore(unsigned value) { sema = CreateSemaphore(NULL, value, MAXLONG, NULL); }
    ~Semaphore() { CloseHandle(sema); }
    void post() { ReleaseSemaphore(sema, 1, NULL); }
    void wait() { WaitForSingleObject(sema, INFINITE); }
};
#else
#error "Unsupported OS"
#endif
