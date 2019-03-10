#include <dlfcn.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DYLD_INTERPOSE(_replacement,_replacee) \
   __attribute__((used)) static struct{ const void* replacement; const void* replacee; } _interpose_##_replacee \
            __attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacement, (const void*)(unsigned long)&_replacee };

int memcpyCount = 0;
uint64_t totalMemcpySize = 0;
int memmoveCount = 0;
uint64_t totalMemmoveSize = 0;
int strlenCount = 0;
uint64_t totalStrlenSize = 0;
int mallocCount = 0;
uint64_t totalMallocSize = 0;
int freeCount = 0;

__attribute__((destructor))
static void destroy()
{
    printf("Memcpy: %d calls, %llu kbytes\n", (int) memcpyCount,
           (unsigned long long) totalMemcpySize / 1024);
    printf("Memmove: %d calls, %llu kbytes\n", (int) memmoveCount,
           (unsigned long long) totalMemmoveSize / 1024);
    printf("Strlen: %d calls, %llu kbytes\n", (int) strlenCount,
           (unsigned long long) totalStrlenSize / 1024);
    printf("Malloc: %d calls, %llu kbytes\n", (int) mallocCount,
           (unsigned long long) totalMallocSize / 1024);
    printf("Free: %d calls\n", (int) freeCount);
}

void* my_memcpy(void* dst, const void* src, size_t size)
{
    memcpyCount++;
    totalMemcpySize += size;
    return memcpy(dst, src, size);
}

DYLD_INTERPOSE(my_memcpy, memcpy)

void* my_memmove(void* dst, const void* src, size_t size)
{
    memmoveCount++;
    totalMemmoveSize += size;
    return memmove(dst, src, size);
}

DYLD_INTERPOSE(my_memmove, memmove)

size_t my_strlen(const char* src)
{
    strlenCount++;
    size_t size = strlen(src);
    totalStrlenSize += size;
    return size;
}

DYLD_INTERPOSE(my_strlen, strlen)

void* my_malloc(size_t size)
{
    mallocCount++;
    totalMallocSize += size;
    return malloc(size);
}

DYLD_INTERPOSE(my_malloc, malloc)

void my_free(void* p)
{
    freeCount++;
    return free(p);
}

DYLD_INTERPOSE(my_free, free)
