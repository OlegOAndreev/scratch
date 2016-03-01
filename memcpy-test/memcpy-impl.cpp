#include <stdint.h>
#include <stdio.h>

// The basic unaligned memcpy: does 8-byte copies, then 1-byte copies the rest.
void naiveMemcpy(char* dst, const char* src, size_t size)
{
    asm volatile(
        // rcx = 8-byte aligned size, rdx = rest of size.
        "  mov %%rdx, %%rcx\n"
        "  and $-8, %%rcx\n"
        "  jz 2f\n"
        "  and $7, %%rdx\n"
        // Do 8-byte iters, rsi = src end, rdi = dst end.
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  mov (%%rsi, %%rcx), %%rax\n"
        "  mov %%rax, (%%rdi, %%rcx)\n"
        "  add $8, %%rcx\n"
        "  jnz 1b\n"
        "2:\n"
        "  test %%rdx, %%rdx\n"
        "  jz 2f\n"
        // Do 1-byte iters, rsi = src end, rdi = dst end.
        "  add %%rdx, %%rsi\n"
        "  add %%rdx, %%rdi\n"
        "  neg %%rdx\n"
        "1:\n"
        "  mov (%%rsi, %%rdx), %%al\n"
        "  mov %%al, (%%rdi, %%rdx)\n"
        "  inc %%rdx\n"
        "  jnz 1b\n"
        "2:\n"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rcx");
}

// The basic unaligned memcpy: aligns the dst with 1-byte copies, does 8-byte copies, then 1-byte copies the rest.
void naiveMemcpyAligned(char* dst, const char* src, size_t size)
{
    asm volatile(
        "  cmp $8, %%rdx\n"
        "  jb 3f\n"
        // rcx = size until the start of the 8-byte aligned dst part.
        "  mov %%rdi, %%rcx\n"
        "  neg %%rcx\n"
        "  and $7, %%rcx\n"
        "  jz 2f\n"
        "  sub %%rcx, %%rdx\n"
        // Do 1-byte iters to align dst, rsi = src end, rdi = dst end.
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  mov (%%rsi, %%rcx), %%al\n"
        "  mov %%al, (%%rdi, %%rcx)\n"
        "  inc %%rcx\n"
        "  jnz 1b\n"
        // Do 8-byte iters, rsi = src end, rdi = dst end.
        "2:\n"
        "  mov %%rdx, %%rcx\n"
        "  and $-8, %%rcx\n"
        "  jz 3f\n"
        "  and $7, %%rdx\n"
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  mov (%%rsi, %%rcx), %%rax\n"
        "  mov %%rax, (%%rdi, %%rcx)\n"
        "  add $8, %%rcx\n"
        "  jnz 1b\n"
        "3:\n"
        "  test %%rdx, %%rdx\n"
        "  jz 2f\n"
        // Do the rest of 1-byte iters, rsi = src end, rdi = dst end.
        "  add %%rdx, %%rsi\n"
        "  add %%rdx, %%rdi\n"
        "  neg %%rdx\n"
        "1:\n"
        "  mov (%%rsi, %%rdx), %%al\n"
        "  mov %%al, (%%rdi, %%rdx)\n"
        "  inc %%rdx\n"
        "  jnz 1b\n"
        "2:\n"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rcx");
}

// The unrolled memcpy: unrolled 8-byte copies (32 per loop), then unrolled copies the rest.
void naiveMemcpyUnrolled(char* dst, const char* src, size_t size)
{
    asm volatile(
        // rcx = 32-byte aligned size, rdx = rest of size.
        "  mov %%rdx, %%rcx\n"
        "  and $-32, %%rcx\n"
        "  jz 2f\n"
        "  and $31, %%rdx\n"
        // Do 32-byte iters, rsi = src end, rdi = dst end.
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  negq %%rcx\n"
        "1:\n"
        "  mov (%%rsi, %%rcx), %%rax\n"
        "  mov 8(%%rsi, %%rcx), %%rbx\n"
        "  mov 16(%%rsi, %%rcx), %%r8\n"
        "  mov 24(%%rsi, %%rcx), %%r9\n"
        "  mov %%rax, (%%rdi, %%rcx)\n"
        "  mov %%rbx, 8(%%rdi, %%rcx)\n"
        "  mov %%r8, 16(%%rdi, %%rcx)\n"
        "  mov %%r9, 24(%%rdi, %%rcx)\n"
        "  add $32, %%rcx\n"
        "  jnz 1b\n"
        // Copy 16, 8, 4, 2, 1 bytes depending on set bits in rdx.
        "2:\n"
        "  test %%rdx, %%rdx\n"
        "  jz 2f\n"
        "  xor %%rcx, %%rcx\n"
        "  test $16, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%rax\n"
        "  mov 8(%%rsi, %%rcx), %%rbx\n"
        "  mov %%rax, (%%rdi, %%rcx)\n"
        "  mov %%rbx, 8(%%rdi, %%rcx)\n"
        "  add $16, %%rcx\n"
        "1:\n"
        "  test $8, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%rax\n"
        "  mov %%rax, (%%rdi, %%rcx)\n"
        "  add $8, %%rcx\n"
        "1:\n"
        "  test $4, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%eax\n"
        "  mov %%eax, (%%rdi, %%rcx)\n"
        "  add $4, %%rcx\n"
        "1:\n"
        "  test $2, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%ax\n"
        "  mov %%ax, (%%rdi, %%rcx)\n"
        "  add $2, %%rcx\n"
        "1:\n"
        "  test $1, %%dl\n"
        "  jz 2f\n"
        "  mov (%%rsi, %%rcx), %%al\n"
        "  mov %%al, (%%rdi, %%rcx)\n"
        "2:\n"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rbx", "rcx", "r8", "r9");
}

// The basic SSE memcpy: aligns the dst with 1-byte copies, does 16-byte copies, then 1-byte copies the rest.
void naiveSseMemcpy(char* dst, const char* src, size_t size)
{
    asm volatile(
        "  cmp $16, %%rdx\n"
        "  jb 3f\n"
        // rcx = size until the start of the 16-byte aligned dst part.
        "  mov %%rdi, %%rcx\n"
        "  neg %%rcx\n"
        "  and $15, %%rcx\n"
        "  jz 2f\n"
        "  sub %%rcx, %%rdx\n"
        // Do 1-byte iters to align dst, rsi = src end, rdi = dst end.
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  mov (%%rsi, %%rcx), %%al\n"
        "  mov %%al, (%%rdi, %%rcx)\n"
        "  inc %%rcx\n"
        "  jnz 1b\n"
        "2:\n"
        "  mov %%rdx, %%rcx\n"
        "  and $-16, %%rcx\n"
        "  jz 3f\n"
        // Do 16-byte iters, rsi = src end, rdi = dst end.
        "  and $15, %%rdx\n"
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  movups (%%rsi, %%rcx), %%xmm0\n"
        "  movaps %%xmm0, (%%rdi, %%rcx)\n"
        "  add $16, %%rcx\n"
        "  jnz 1b\n"
        "3:\n"
        "  test %%rdx, %%rdx\n"
        "  jz 2f\n"
        // Do the rest of 1-byte iters, rsi = src end, rdi = dst end.
        "  add %%rdx, %%rsi\n"
        "  add %%rdx, %%rdi\n"
        "  neg %%rdx\n"
        "1:\n"
        "  mov (%%rsi, %%rdx), %%al\n"
        "  mov %%al, (%%rdi, %%rdx)\n"
        "  inc %%rdx\n"
        "  jnz 1b\n"
        "2:\n"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rcx", "xmm0");
}

// The basic SSE memcpy with unrolled body: aligns the dst with 1-byte copies, does 32-byte copies per loop, then
// 1-byte copies the rest.
void naiveSseMemcpyUnrolledBody(char* dst, const char* src, size_t size)
{
    asm volatile(
    "  cmp $32, %%rdx\n"
        "  jb 3f\n"
        // rcx = size until the start of the 16-byte aligned dst part.
        "  mov %%rdi, %%rcx\n"
        "  neg %%rcx\n"
        "  and $15, %%rcx\n"
        "  jz 2f\n"
        "  sub %%rcx, %%rdx\n"
        // Do 1-byte iters to align dst, rsi = src end, rdi = dst end.
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  mov (%%rsi, %%rcx), %%al\n"
        "  mov %%al, (%%rdi, %%rcx)\n"
        "  inc %%rcx\n"
        "  jnz 1b\n"
        "2:\n"
        "  mov %%rdx, %%rcx\n"
        "  and $-32, %%rcx\n"
        "  jz 3f\n"
        // Do 32-byte iters, rsi = src end, rdi = dst end.
        "  and $31, %%rdx\n"
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  movups (%%rsi, %%rcx), %%xmm0\n"
        "  movups 16(%%rsi, %%rcx), %%xmm1\n"
        "  movaps %%xmm0, (%%rdi, %%rcx)\n"
        "  movaps %%xmm1, 16(%%rdi, %%rcx)\n"
        "  add $32, %%rcx\n"
        "  jnz 1b\n"
        "3:\n"
        "  test %%rdx, %%rdx\n"
        "  jz 2f\n"
        // Do the rest of 1-byte iters, rsi = src end, rdi = dst end.
        "  add %%rdx, %%rsi\n"
        "  add %%rdx, %%rdi\n"
        "  neg %%rdx\n"
        "1:\n"
        "  mov (%%rsi, %%rdx), %%al\n"
        "  mov %%al, (%%rdi, %%rdx)\n"
        "  inc %%rdx\n"
        "  jnz 1b\n"
        "2:\n"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rcx", "xmm0", "xmm1");
}

// The basic unrolled SSE memcpy: aligns dst with unrolled copies, does 32-byte copies per loop, then unrolled
// copies the rest.
void naiveSseMemcpyUnrolled(char* dst, const char* src, size_t size)
{
    asm volatile(
        "  cmp $32, %%rdx\n"
        "  jb 3f\n"
        // rcx = size until the start of the 16-byte aligned dst part.
        "  mov %%rdi, %%rcx\n"
        "  neg %%rcx\n"
        "  and $15, %%rcx\n"
        "  jz 2f\n"
        "  sub %%rcx, %%rdx\n"
        // Copy 1, 2, 4, 8 bytes depending on set bits in rcx.
        "  xor %%r8, %%r8\n"
        "  test $1, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi), %%al\n"
        "  mov %%al, (%%rdi)\n"
        "  inc %%r8\n"
        "1:\n"
        "  test $2, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%r8), %%ax\n"
        "  mov %%ax, (%%rdi, %%r8)\n"
        "  add $2, %%r8\n"
        "1:\n"
        "  test $4, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%r8), %%eax\n"
        "  mov %%eax, (%%rdi, %%r8)\n"
        "  add $4, %%r8\n"
        "1:\n"
        "  test $8, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%r8), %%rax\n"
        "  mov %%rax, (%%rdi, %%r8)\n"
        "  add $8, %%r8\n"
        "1:\n"
        "  add %%r8, %%rsi\n"
        "  add %%r8, %%rdi\n"
        "2:\n"
        "  mov %%rdx, %%rcx\n"
        "  and $-32, %%rcx\n"
        "  jz 3f\n"
        // Do 32-byte iters, rsi = src end, rdi = dst end.
        "  and $31, %%rdx\n"
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  movups (%%rsi, %%rcx), %%xmm0\n"
        "  movups 16(%%rsi, %%rcx), %%xmm1\n"
        "  movaps %%xmm0, (%%rdi, %%rcx)\n"
        "  movaps %%xmm1, 16(%%rdi, %%rcx)\n"
        "  add $32, %%rcx\n"
        "  jnz 1b\n"
        // Copy 16, 8, 4, 2, 1 bytes depending on set bits in rdx.
        "3:\n"
        "  test %%rdx, %%rdx\n"
        "  jz 2f\n"
        "  xor %%rcx, %%rcx\n"
        "  test $16, %%dl\n"
        "  jz 1f\n"
        "  movups (%%rsi), %%xmm0\n"
        "  movups %%xmm0, (%%rdi)\n"
        "  add $16, %%rcx\n"
        "1:\n"
        "  test $8, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%rax\n"
        "  mov %%rax, (%%rdi, %%rcx)\n"
        "  add $8, %%rcx\n"
        "1:\n"
        "  test $4, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%eax\n"
        "  mov %%eax, (%%rdi, %%rcx)\n"
        "  add $4, %%rcx\n"
        "1:\n"
        "  test $2, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%ax\n"
        "  mov %%ax, (%%rdi, %%rcx)\n"
        "  add $2, %%rcx\n"
        "1:\n"
        "  test $1, %%dl\n"
        "  jz 2f\n"
        "  mov (%%rsi, %%rcx), %%al\n"
        "  mov %%al, (%%rdi, %%rcx)\n"
        "2:\n"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rcx", "r8", "xmm0", "xmm1");
}


// The basic unrolled SSE memcpy with non-temporal moves: aligns dst with unrolled copies, does 32-byte copies per loop,
// then unrolled copies the rest.
void naiveSseMemcpyUnrolledNT(char* dst, const char* src, size_t size)
{
    asm volatile(
    "  cmp $32, %%rdx\n"
        "  jb 3f\n"
        // rcx = size until the start of the 16-byte aligned dst part.
        "  mov %%rdi, %%rcx\n"
        "  neg %%rcx\n"
        "  and $15, %%rcx\n"
        "  jz 2f\n"
        "  sub %%rcx, %%rdx\n"
        // Copy 1, 2, 4, 8 bytes depending on set bits in rcx.
        "  xor %%r8, %%r8\n"
        "  test $1, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi), %%al\n"
        "  mov %%al, (%%rdi)\n"
        "  inc %%r8\n"
        "1:\n"
        "  test $2, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%r8), %%ax\n"
        "  mov %%ax, (%%rdi, %%r8)\n"
        "  add $2, %%r8\n"
        "1:\n"
        "  test $4, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%r8), %%eax\n"
        "  mov %%eax, (%%rdi, %%r8)\n"
        "  add $4, %%r8\n"
        "1:\n"
        "  test $8, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%r8), %%rax\n"
        "  mov %%rax, (%%rdi, %%r8)\n"
        "  add $8, %%r8\n"
        "1:\n"
        "  add %%r8, %%rsi\n"
        "  add %%r8, %%rdi\n"
        "2:\n"
        "  mov %%rdx, %%rcx\n"
        "  and $-32, %%rcx\n"
        "  jz 3f\n"
        // Do 32-byte iters, rsi = src end, rdi = dst end.
        "  and $31, %%rdx\n"
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  movups (%%rsi, %%rcx), %%xmm0\n"
        "  movups 16(%%rsi, %%rcx), %%xmm1\n"
        "  movntps %%xmm0, (%%rdi, %%rcx)\n"
        "  movntps %%xmm1, 16(%%rdi, %%rcx)\n"
        "  add $32, %%rcx\n"
        "  jnz 1b\n"
        "  sfence\n"
        // Copy 16, 8, 4, 2, 1 bytes depending on set bits in rdx.
        "3:\n"
        "  test %%rdx, %%rdx\n"
        "  jz 2f\n"
        "  xor %%rcx, %%rcx\n"
        "  test $16, %%dl\n"
        "  jz 1f\n"
        "  movups (%%rsi), %%xmm0\n"
        "  movups %%xmm0, (%%rdi)\n"
        "  add $16, %%rcx\n"
        "1:\n"
        "  test $8, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%rax\n"
        "  mov %%rax, (%%rdi, %%rcx)\n"
        "  add $8, %%rcx\n"
        "1:\n"
        "  test $4, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%eax\n"
        "  mov %%eax, (%%rdi, %%rcx)\n"
        "  add $4, %%rcx\n"
        "1:\n"
        "  test $2, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%ax\n"
        "  mov %%ax, (%%rdi, %%rcx)\n"
        "  add $2, %%rcx\n"
        "1:\n"
        "  test $1, %%dl\n"
        "  jz 2f\n"
        "  mov (%%rsi, %%rcx), %%al\n"
        "  mov %%al, (%%rdi, %%rcx)\n"
        "2:\n"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rcx", "r8", "xmm0", "xmm1");
}
// The basic AVX memcpy: aligns the dst with 1-byte copies, does 32-byte copies, then 1-byte copies the rest.
void naiveAvxMemcpy(char* dst, const char* src, size_t size)
{
    asm volatile(
        "  cmp $32, %%rdx\n"
        "  jb 3f\n"
        // rcx = size until the start of the 32-byte aligned dst part.
        "  mov %%rdi, %%rcx\n"
        "  neg %%rcx\n"
        "  and $31, %%rcx\n"
        "  jz 2f\n"
        "  sub %%rcx, %%rdx\n"
        // Do 1-byte iters to align dst, rsi = src end, rdi = dst end.
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  mov (%%rsi, %%rcx), %%al\n"
        "  mov %%al, (%%rdi, %%rcx)\n"
        "  inc %%rcx\n"
        "  jnz 1b\n"
        "2:\n"
        "  mov %%rdx, %%rcx\n"
        "  and $-32, %%rcx\n"
        "  jz 3f\n"
        // Do 32-byte iters, rsi = src end, rdi = dst end.
        "  and $31, %%rdx\n"
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  vmovups (%%rsi, %%rcx), %%ymm0\n"
        "  vmovaps %%ymm0, (%%rdi, %%rcx)\n"
        "  add $32, %%rcx\n"
        "  jnz 1b\n"
        "3:\n"
        "  test %%rdx, %%rdx\n"
        "  jz 2f\n"
        // Do the rest of 1-byte iters, rsi = src end, rdi = dst end.
        "  add %%rdx, %%rsi\n"
        "  add %%rdx, %%rdi\n"
        "  neg %%rdx\n"
        "1:\n"
        "  mov (%%rsi, %%rdx), %%al\n"
        "  mov %%al, (%%rdi, %%rdx)\n"
        "  inc %%rdx\n"
        "  jnz 1b\n"
        "2:\n"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rcx", "ymm0");
}

// The basic unrolled AVX memcpy: aligns dst with unrolled copies, does 64-byte copies per loop, then unrolled
// copies the rest.
void naiveAvxMemcpyUnrolled(char* dst, const char* src, size_t size)
{
    asm volatile(
        "  cmp $64, %%rdx\n"
        "  jb 3f\n"
        // rcx = size until the start of the 32-byte aligned dst part.
        "  mov %%rdi, %%rcx\n"
        "  neg %%rcx\n"
        "  and $31, %%rcx\n"
        "  jz 2f\n"
        "  sub %%rcx, %%rdx\n"
        // Copy 1, 2, 4, 8, 16 bytes depending on set bits in rcx.
        "  xor %%r8, %%r8\n"
        "  test $1, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi), %%al\n"
        "  mov %%al, (%%rdi)\n"
        "  inc %%r8\n"
        "1:\n"
        "  test $2, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%r8), %%ax\n"
        "  mov %%ax, (%%rdi, %%r8)\n"
        "  add $2, %%r8\n"
        "1:\n"
        "  test $4, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%r8), %%eax\n"
        "  mov %%eax, (%%rdi, %%r8)\n"
        "  add $4, %%r8\n"
        "1:\n"
        "  test $8, %%cl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%r8), %%rax\n"
        "  mov %%rax, (%%rdi, %%r8)\n"
        "  add $8, %%r8\n"
        "1:\n"
        "  test $16, %%cl\n"
        "  jz 1f\n"
        "  movups (%%rsi, %%r8), %%xmm0\n"
        "  movaps %%xmm0, (%%rdi, %%r8)\n"
        "  add $16, %%r8\n"
        "1:\n"
        "  add %%r8, %%rsi\n"
        "  add %%r8, %%rdi\n"
        "2:\n"
        "  mov %%rdx, %%rcx\n"
        "  and $-64, %%rcx\n"
        "  jz 3f\n"
        // Do 64-byte iters, rsi = src end, rdi = dst end.
        "  and $63, %%rdx\n"
        "  add %%rcx, %%rsi\n"
        "  add %%rcx, %%rdi\n"
        "  neg %%rcx\n"
        "1:\n"
        "  vmovups (%%rsi, %%rcx), %%ymm0\n"
        "  vmovups 32(%%rsi, %%rcx), %%ymm1\n"
        "  vmovaps %%ymm0, (%%rdi, %%rcx)\n"
        "  vmovaps %%ymm1, 32(%%rdi, %%rcx)\n"
        "  add $64, %%rcx\n"
        "  jnz 1b\n"
        // Copy 32, 16, 8, 4, 2, 1 bytes depending on set bits in rdx.
        "3:\n"
        "  test %%rdx, %%rdx\n"
        "  jz 2f\n"
        "  xor %%rcx, %%rcx\n"
        "  test $32, %%rdx\n"
        "  jz 1f\n"
        "  vmovups (%%rsi), %%ymm0\n"
        "  vmovups %%ymm0, (%%rdi)\n"
        "  add $32, %%rcx\n"
        "1:\n"
        "  test $16, %%dl\n"
        "  jz 1f\n"
        "  movups (%%rsi, %%rcx), %%xmm0\n"
        "  movups %%xmm0, (%%rdi, %%rcx)\n"
        "  add $16, %%rcx\n"
        "1:\n"
        "  test $8, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%rax\n"
        "  mov %%rax, (%%rdi, %%rcx)\n"
        "  add $8, %%rcx\n"
        "1:\n"
        "  test $4, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%eax\n"
        "  mov %%eax, (%%rdi, %%rcx)\n"
        "  add $4, %%rcx\n"
        "1:\n"
        "  test $2, %%dl\n"
        "  jz 1f\n"
        "  mov (%%rsi, %%rcx), %%ax\n"
        "  mov %%ax, (%%rdi, %%rcx)\n"
        "  add $2, %%rcx\n"
        "1:\n"
        "  test $1, %%dl\n"
        "  jz 2f\n"
        "  mov (%%rsi, %%rcx), %%al\n"
        "  mov %%al, (%%rdi, %%rcx)\n"
        "2:\n"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rcx", "r8", "ymm0", "ymm1");
}

void repMovsbMemcpy(char* dst, const char* src, size_t size)
{
    asm volatile("rep movsb" : "+S" (src), "+D" (dst), "+c" (size) : : "memory", "cc");
}

void repMovsqMemcpy(char* dst, const char* src, size_t size)
{
    asm volatile(
        "  mov %%rcx, %%rdx\n"
        "  shr $3, %%rcx\n"
        "  jz 1f\n"
        "  and $7, %%rdx\n"
        "  rep movsq\n"
        "1:\n"
        "  mov %%rdx, %%rcx\n"
        "  rep movsb"
    : "+S" (src), "+D" (dst), "+c" (size)
    : : "memory", "cc", "rdx");
}

bool isAvxSupported()
{
    uint32_t eax, ebx, ecx, edx;
    asm("mov $1, %%eax\ncpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : : );
    bool osxsave = (ecx & (1 << 27)) != 0;
    bool avx = (ecx & (1 << 28)) != 0;
    return osxsave && avx;
}

// A copy of memcpy from musl src/string/x86_64/memcpy.s
void memcpyFromMusl(char* dst, const char* src, size_t size)
{
    asm volatile(
        "  mov %%rdi, %%rax\n"
        "  cmp $8, %%rdx\n"
        "  jc 1f\n"
        "  test $7, %%edi\n"
        "  jz 1f\n"
        "2:  movsb\n"
        "  dec %%rdx\n"
        "  test $7, %%edi\n"
        "  jnz 2b\n"
        "1:  mov %%rdx, %%rcx\n"
        "  shr $3, %%rcx\n"
        "  rep\n"
        "  movsq\n"
        "  and $7, %%edx\n"
        "  jz 1f\n"
        "2:  movsb\n"
        "  dec %%edx\n"
        "  jnz 2b\n"
        "1:"
    : "+S" (src), "+D" (dst), "+d" (size)
    : : "memory", "cc", "rax", "rcx");
}
