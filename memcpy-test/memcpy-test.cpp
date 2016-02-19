#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>

// Fucking strict aliasing.
uint64_t u64_load(const char* src)
{
    uint64_t ret;
    memcpy(&ret, src, sizeof(ret));
    return ret;
}

void u64_store(uint64_t v, char* dst)
{
    memcpy(dst, &v, sizeof(v));
}

template <size_t align, typename T>
T* alignedPtr(T* p)
{
    return (T*)(((uintptr_t)p + (align - 1)) & ~(align - 1));
}

template <size_t align, typename T>
const T* alignedPtr(const T* p)
{
    return (const T*)(((uintptr_t)p + (align - 1)) & ~(align - 1));
}

template <size_t align, typename T>
const T* alignedEnd(const T* p)
{
    return (const T*)((uintptr_t)p & ~(align - 1));
}

template <size_t align, typename T>
bool isAligned(const T* p)
{
    return (((uintptr_t)p) & (align - 1)) == 0;
}

void libcMemcpy(void* dst, const void* src, size_t size)
{
    memmove(dst, src, size);
}

void naiveMemcpy(void* dst, const void* src, size_t size)
{
    char* cdst = (char*)dst;
    const char* csrc = (char*)src;
    const char* end = csrc + size;
    const char* aend = alignedEnd<8>(end);
    while (csrc != aend) {
        u64_store(u64_load(csrc), cdst);
        csrc += 8;
        cdst += 8;
    }
    while (csrc != end) {
        *cdst = *csrc;
        csrc++;
        cdst++;
    }
}

void naiveSseMemcpy(void* dst, const void* src, size_t size)
{
    if (size < 16) {
        naiveMemcpy(dst, src, size);
        return;
    }

    char* cdst = (char*)dst;
    const char* csrc = (char*)src;
    const char* end = csrc + size;
    const char* abegin = alignedPtr<16>(csrc);
    const char* aend = alignedEnd<16>(end);

    while (csrc != abegin) {
        *cdst = *csrc;
        csrc++;
        cdst++;
    }
    if (isAligned<16>(cdst)) {
        while (csrc != aend) {
            _mm_store_ps((float*)cdst, _mm_load_ps((float*)csrc));
            csrc += 16;
            cdst += 16;
        }
    } else {
        // Use proper shift + aligned store.
        while (csrc != aend) {
            _mm_storeu_ps((float*)cdst, _mm_load_ps((float*)csrc));
            csrc += 16;
            cdst += 16;
        }
    }
    while (csrc != end) {
        *cdst = *csrc;
        csrc++;
        cdst++;
    }
}

void unrolled2xSseMemcpy(void* dst, const void* src, size_t size)
{
    if (size < 32) {
        naiveMemcpy(dst, src, size);
        return;
    }

    char* cdst = (char*)dst;
    const char* csrc = (char*)src;
    const char* end = csrc + size;
    const char* abegin = alignedPtr<32>(csrc);
    const char* aend = alignedEnd<32>(end);

    while (csrc != abegin) {
        *cdst = *csrc;
        csrc++;
        cdst++;
    }
    if (isAligned<16>(cdst)) {
        while (csrc != aend) {
            __m128 m0 = _mm_load_ps((float*)csrc);
            csrc += 16;
            __m128 m1 = _mm_load_ps((float*)csrc);
            csrc += 16;
            _mm_store_ps((float*)cdst, m0);
            cdst += 16;
            _mm_store_ps((float*)cdst, m1);
            cdst += 16;
        }
    } else {
        while (csrc != aend) {
            // Use proper shift + aligned store.
            __m128 m0 = _mm_load_ps((float*)csrc);
            csrc += 16;
            __m128 m1 = _mm_load_ps((float*)csrc);
            csrc += 16;
            _mm_storeu_ps((float*)cdst, m0);
            cdst += 16;
            _mm_storeu_ps((float*)cdst, m1);
            cdst += 16;
        }
    }
    while (csrc != end) {
        *cdst = *csrc;
        csrc++;
        cdst++;
    }
}

void unrolled4xSseMemcpy(void* dst, const void* src, size_t size)
{
    if (size < 64) {
        naiveMemcpy(dst, src, size);
        return;
    }

    char* cdst = (char*)dst;
    const char* csrc = (char*)src;
    const char* end = csrc + size;
    const char* abegin = alignedPtr<64>(csrc);
    const char* aend = alignedEnd<64>(end);

    while (csrc != abegin) {
        *cdst = *csrc;
        csrc++;
        cdst++;
    }
    if (isAligned<16>(cdst)) {
        while (csrc != aend) {
            __m128 m0 = _mm_load_ps((float*)csrc);
            csrc += 16;
            __m128 m1 = _mm_load_ps((float*)csrc);
            csrc += 16;
            __m128 m2 = _mm_load_ps((float*)csrc);
            csrc += 16;
            __m128 m3 = _mm_load_ps((float*)csrc);
            csrc += 16;
            _mm_store_ps((float*)cdst, m0);
            cdst += 16;
            _mm_store_ps((float*)cdst, m1);
            cdst += 16;
            _mm_store_ps((float*)cdst, m2);
            cdst += 16;
            _mm_store_ps((float*)cdst, m3);
            cdst += 16;
        }
    } else {
        while (csrc != aend) {
            // Use proper shift + aligned store.
            __m128 m0 = _mm_load_ps((float*)csrc);
            csrc += 16;
            __m128 m1 = _mm_load_ps((float*)csrc);
            csrc += 16;
            __m128 m2 = _mm_load_ps((float*)csrc);
            csrc += 16;
            __m128 m3 = _mm_load_ps((float*)csrc);
            csrc += 16;
            _mm_storeu_ps((float*)cdst, m0);
            cdst += 16;
            _mm_storeu_ps((float*)cdst, m1);
            cdst += 16;
            _mm_storeu_ps((float*)cdst, m2);
            cdst += 16;
            _mm_storeu_ps((float*)cdst, m3);
            cdst += 16;
        }
    }
    while (csrc != end) {
        *cdst = *csrc;
        csrc++;
        cdst++;
    }
}

void repMovsbMemcpy(void* dst, const void* src, size_t size)
{
    asm("rep movsb" : : "S" (src), "D" (dst), "c" (size) : "%rcx", "%rsi", "%rdi");
}

void repMovsqMemcpy(void* dst, const void* src, size_t size)
{
    asm("rep movsq\n"
        "movq %0, %%rcx"
        : : "S" (src), "D" (dst), "c" (size / 8), "r" (size & 7) : "%rcx", "%rsi", "%rdi");
}

// void muslMemcpy(void* dst, const void* src, size_t size)
// {
//     asm("movq %%rdi, %%rax\n"
//         "cmpq $8, %%rdx\n"
//         "jc 1f\n"
//         "testl $7, %%edi\n"
//         "jz 1f\n"
// "2:      movsb\n"
//         "decq %%rdx\n"
//         "testl $7, %%edi\n"
//         "jnz 2b\n"
// "1:      movq %%rdx, %%rcx\n"
//         "shrq $3, %%rcx\n"
//         "rep movsq\n"
//         "andl $7, %%edx\n"
//         "jz 1f\n"
// "2:      movsb\n"
//         "decl %%edx\n"
//         "jnz 2b\n"
//     :
//     :
//     );
// }