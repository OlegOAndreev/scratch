.text

.global _isAvxSupported
_isAvxSupported:
    push %rbx
    mov $1, %eax
    cpuid
    /* Test if osxsave and avx bits are set */
    and $0x18000000, %ecx
    cmp $0x18000000, %ecx
    sete %ah
    pop %rbx
    ret

/* The basic unaligned memcpy: does 8-byte copies, then 1-byte copies the rest */
.global _naiveMemcpy
_naiveMemcpy:
    /* rcx = 8-byte aligned size, rdx = rest of size */
    mov %rdx, %rcx
    and $-8, %rcx
    jz 2f
    and $7, %rdx
    /* Do 8-byte iters, rsi = src end, rdi = dst end. */
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  mov (%rsi, %rcx), %rax
    mov %rax, (%rdi, %rcx)
    add $8, %rcx
    jnz 1b
2:  test %rdx, %rdx
    jz 2f
    /* Do 1-byte iters, rsi = src end, rdi = dst end */
    add %rdx, %rsi
    add %rdx, %rdi
    neg %rdx
1:  movzbl (%rsi, %rdx), %eax
    mov %al, (%rdi, %rdx)
    inc %rdx
    jnz 1b
    ret

// The basic unaligned memcpy: aligns the dst with 1-byte copies, does 8-byte copies, then 1-byte copies the rest. */
.global _naiveMemcpyAligned
_naiveMemcpyAligned:
    cmp $8, %rdx
    jb 3f
    /* rcx = size until the start of the 8-byte aligned dst part. */
    mov %rdi, %rcx
    neg %rcx
    and $7, %rcx
    jz 2f
    sub %rcx, %rdx
    /* Do 1-byte iters to align dst, rsi = src end, rdi = dst end. */
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  movzbl (%rsi, %rcx), %eax
    mov %al, (%rdi, %rcx)
    inc %rcx
    jnz 1b
    /* Do 8-byte iters, rsi = src end, rdi = dst end. */
2:  mov %rdx, %rcx
    and $-8, %rcx
    jz 3f
    and $7, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:
    mov (%rsi, %rcx), %rax
    mov %rax, (%rdi, %rcx)
    add $8, %rcx
    jnz 1b
3:  test %rdx, %rdx
    jz 2f
    /* Do the rest of 1-byte iters, rsi = src end, rdi = dst end. */
    add %rdx, %rsi
    add %rdx, %rdi
    neg %rdx
1:  movzbl (%rsi, %rdx), %eax
    mov %al, (%rdi, %rdx)
    inc %rdx
    jnz 1b
2:  ret

/* The unrolled memcpy: unrolled 8-byte copies (32 per loop), then unrolled copies the rest. */
.global _naiveMemcpyUnrolled
_naiveMemcpyUnrolled:
    /* rcx = 32-byte aligned size, rdx = rest of size. */
    mov %rdx, %rcx
    and $-32, %rcx
    jz 2f
    and $31, %rdx
    /* Do 32-byte iters, rsi = src end, rdi = dst end. */
    add %rcx, %rsi
    add %rcx, %rdi
    negq %rcx
1:  mov (%rsi, %rcx), %rax
    mov 8(%rsi, %rcx), %r8
    mov 16(%rsi, %rcx), %r9
    mov 24(%rsi, %rcx), %r10
    mov %rax, (%rdi, %rcx)
    mov %r8, 8(%rdi, %rcx)
    mov %r9, 16(%rdi, %rcx)
    mov %r10, 24(%rdi, %rcx)
    add $32, %rcx
    jnz 1b
    /* Copy 16, 8, 4, 2, 1 bytes depending on set bits in rdx. */
2:  test %rdx, %rdx
    jz 2f
    xor %rcx, %rcx
    test $16, %dl
    jz 1f
    mov (%rsi, %rcx), %rax
    mov 8(%rsi, %rcx), %r8
    mov %rax, (%rdi, %rcx)
    mov %r8, 8(%rdi, %rcx)
    add $16, %rcx
1:  test $8, %dl
    jz 1f
    mov (%rsi, %rcx), %rax
    mov %rax, (%rdi, %rcx)
    add $8, %rcx
1:  test $4, %dl
    jz 1f
    mov (%rsi, %rcx), %eax
    mov %eax, (%rdi, %rcx)
    add $4, %rcx
1:  test $2, %dl
    jz 1f
    movzwl (%rsi, %rcx), %eax
    mov %ax, (%rdi, %rcx)
    add $2, %rcx
1:  test $1, %dl
    jz 2f
    movzbl (%rsi, %rcx), %eax
    mov %al, (%rdi, %rcx)
2:  ret

/* The basic SSE memcpy: aligns the dst with 1-byte copies, does 16-byte copies, then 1-byte copies the rest. */
.global _naiveSseMemcpy
_naiveSseMemcpy:
    mov %rdx, %rcx
    and $-16, %rcx
    jz 3f
    /* Do 16-byte iters, rsi = src end, rdi = dst end. */
    and $15, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  movups (%rsi, %rcx), %xmm0
    movups %xmm0, (%rdi, %rcx)
    add $16, %rcx
    jnz 1b
3:  test %rdx, %rdx
    jz 2f
    /* Do the rest of 1-byte iters, rsi = src end, rdi = dst end. */
    add %rdx, %rsi
    add %rdx, %rdi
    neg %rdx
1:  movzbl (%rsi, %rdx), %eax
    mov %al, (%rdi, %rdx)
    inc %rdx
    jnz 1b
2:  ret

/* The basic SSE memcpy with alignment: aligns the dst with 1-byte copies, does 16-byte copies, then 1-byte copies
   the rest. */
.global _naiveSseMemcpyAligned
_naiveSseMemcpyAligned:
    cmp $16, %rdx
    jb 3f
    /* rcx = size until the start of the 16-byte aligned dst part. */
    mov %rdi, %rcx
    neg %rcx
    and $15, %rcx
    jz 2f
    sub %rcx, %rdx
    /* Do 1-byte iters to align dst, rsi = src end, rdi = dst end. */
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  movzbl (%rsi, %rcx), %eax
    mov %al, (%rdi, %rcx)
    inc %rcx
    jnz 1b
2:  mov %rdx, %rcx
    and $-16, %rcx
    jz 3f
    /* Do 16-byte iters, rsi = src end, rdi = dst end. */
    and $15, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  movups (%rsi, %rcx), %xmm0
    movaps %xmm0, (%rdi, %rcx)
    add $16, %rcx
    jnz 1b
3:  test %rdx, %rdx
    jz 2f
    /* Do the rest of 1-byte iters, rsi = src end, rdi = dst end. */
    add %rdx, %rsi
    add %rdx, %rdi
    neg %rdx
1:  movzbl (%rsi, %rdx), %eax
    mov %al, (%rdi, %rdx)
    inc %rdx
    jnz 1b
2:  ret

// The basic SSE memcpy with unrolled body: aligns the dst with 1-byte copies, does 32-byte copies per loop, then
// 1-byte copies the rest.
.global _naiveSseMemcpyUnrolledBody
_naiveSseMemcpyUnrolledBody:
    cmp $32, %rdx
    jb 3f
    /* rcx = size until the start of the 16-byte aligned dst part. */
    mov %rdi, %rcx
    neg %rcx
    and $15, %rcx
    jz 2f
    sub %rcx, %rdx
    /* Do 1-byte iters to align dst, rsi = src end, rdi = dst end. */
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  movzbl (%rsi, %rcx), %eax
    mov %al, (%rdi, %rcx)
    inc %rcx
    jnz 1b
2:  mov %rdx, %rcx
    and $-32, %rcx
    jz 3f
    /* Do 32-byte iters, rsi = src end, rdi = dst end. */
    and $31, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  movups (%rsi, %rcx), %xmm0
    movups 16(%rsi, %rcx), %xmm1
    movaps %xmm0, (%rdi, %rcx)
    movaps %xmm1, 16(%rdi, %rcx)
    add $32, %rcx
    jnz 1b
3:  test %rdx, %rdx
    jz 2f
    /* Do the rest of 1-byte iters, rsi = src end, rdi = dst end. */
    add %rdx, %rsi
    add %rdx, %rdi
    neg %rdx
1:  movzbl (%rsi, %rdx), %eax
    mov %al, (%rdi, %rdx)
    inc %rdx
    jnz 1b
2:  ret

/* The basic unrolled SSE memcpy: aligns dst with unrolled copies, does 32-byte copies per loop, then unrolled
   copies the rest. */
.global _naiveSseMemcpyUnrolled
_naiveSseMemcpyUnrolled:
    cmp $32, %rdx
    jb 3f
    /* rcx = size until the start of the 16-byte aligned dst part. */
    mov %rdi, %rcx
    neg %rcx
    and $15, %rcx
    jz 2f
    sub %rcx, %rdx
    /* Copy 1, 2, 4, 8 bytes depending on set bits in rcx. */
    xor %r8, %r8
    test $1, %cl
    jz 1f
    movzbl (%rsi), %eax
    mov %al, (%rdi)
    inc %r8
1:  test $2, %cl
    jz 1f
    movzwl (%rsi, %r8), %eax
    mov %ax, (%rdi, %r8)
    add $2, %r8
1:  test $4, %cl
    jz 1f
    mov (%rsi, %r8), %eax
    mov %eax, (%rdi, %r8)
    add $4, %r8
1:  test $8, %cl
    jz 1f
    mov (%rsi, %r8), %rax
    mov %rax, (%rdi, %r8)
    add $8, %r8
1:  add %r8, %rsi
    add %r8, %rdi
2:  mov %rdx, %rcx
    and $-32, %rcx
    jz 3f
    /* Do 32-byte iters, rsi = src end, rdi = dst end. */
    and $31, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  movups (%rsi, %rcx), %xmm0
    movups 16(%rsi, %rcx), %xmm1
    movaps %xmm0, (%rdi, %rcx)
    movaps %xmm1, 16(%rdi, %rcx)
    add $32, %rcx
    jnz 1b
    /* Copy 16, 8, 4, 2, 1 bytes depending on set bits in rdx. */
3:  test %rdx, %rdx
    jz 2f
    xor %rcx, %rcx
    test $16, %dl
    jz 1f
    movups (%rsi), %xmm0
    movups %xmm0, (%rdi)
    add $16, %rcx
1:  test $8, %dl
    jz 1f
    mov (%rsi, %rcx), %rax
    mov %rax, (%rdi, %rcx)
    add $8, %rcx
1:  test $4, %dl
    jz 1f
    mov (%rsi, %rcx), %eax
    mov %eax, (%rdi, %rcx)
    add $4, %rcx
1:  test $2, %dl
    jz 1f
    movzwl (%rsi, %rcx), %eax
    mov %ax, (%rdi, %rcx)
    add $2, %rcx
1:  test $1, %dl
    jz 2f
    movzbl (%rsi, %rcx), %eax
    mov %al, (%rdi, %rcx)
2:  ret

/* The basic unrolled SSE memcpy with non-temporal moves: aligns dst with unrolled copies, does 32-byte copies per loop,
   then unrolled copies the rest. */
.global _naiveSseMemcpyUnrolledNT
_naiveSseMemcpyUnrolledNT:
    cmp $32, %rdx
    jb 3f
    /* rcx = size until the start of the 16-byte aligned dst part. */
    mov %rdi, %rcx
    neg %rcx
    and $15, %rcx
    jz 2f
    sub %rcx, %rdx
    /* Copy 1, 2, 4, 8 bytes depending on set bits in rcx. */
    xor %r8, %r8
    test $1, %cl
    jz 1f
    movzbl (%rsi), %eax
    mov %al, (%rdi)
    inc %r8
1:  test $2, %cl
    jz 1f
    movzwl (%rsi, %r8), %eax
    mov %ax, (%rdi, %r8)
    add $2, %r8
1:  test $4, %cl
    jz 1f
    mov (%rsi, %r8), %eax
    mov %eax, (%rdi, %r8)
    add $4, %r8
1:  test $8, %cl
    jz 1f
    mov (%rsi, %r8), %rax
    mov %rax, (%rdi, %r8)
    add $8, %r8
1:  add %r8, %rsi
    add %r8, %rdi
2:  mov %rdx, %rcx
    and $-32, %rcx
    jz 3f
    /* Do 32-byte iters, rsi = src end, rdi = dst end. */
    and $31, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  movups (%rsi, %rcx), %xmm0
    movups 16(%rsi, %rcx), %xmm1
    movntps %xmm0, (%rdi, %rcx)
    movntps %xmm1, 16(%rdi, %rcx)
    add $32, %rcx
    jnz 1b
    sfence
    /* Copy 16, 8, 4, 2, 1 bytes depending on set bits in rdx. */
3:  test %rdx, %rdx
    jz 2f
    xor %rcx, %rcx
    test $16, %dl
    jz 1f
    movups (%rsi), %xmm0
    movups %xmm0, (%rdi)
    add $16, %rcx
1:  test $8, %dl
    jz 1f
    mov (%rsi, %rcx), %rax
    mov %rax, (%rdi, %rcx)
    add $8, %rcx
1:  test $4, %dl
    jz 1f
    mov (%rsi, %rcx), %eax
    mov %eax, (%rdi, %rcx)
    add $4, %rcx
1:  test $2, %dl
    jz 1f
    movzwl (%rsi, %rcx), %eax
    mov %ax, (%rdi, %rcx)
    add $2, %rcx
1:  test $1, %dl
    jz 2f
    movzbl (%rsi, %rcx), %eax
    mov %al, (%rdi, %rcx)
2:  ret

/* The basic AVX memcpy: aligns the dst with 1-byte copies, does 32-byte copies, then 1-byte copies the rest. */
.global _naiveAvxMemcpy
_naiveAvxMemcpy:
    cmp $32, %rdx
    jb 3f
    /* rcx = size until the start of the 32-byte aligned dst part. */
    mov %rdi, %rcx
    neg %rcx
    and $31, %rcx
    jz 2f
    sub %rcx, %rdx
    /* Do 1-byte iters to align dst, rsi = src end, rdi = dst end. */
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  movzbl (%rsi, %rcx), %eax
    mov %al, (%rdi, %rcx)
    inc %rcx
    jnz 1b
2:  mov %rdx, %rcx
    and $-32, %rcx
    jz 3f
    /* Do 32-byte iters, rsi = src end, rdi = dst end. */
    and $31, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
1:  vmovups (%rsi, %rcx), %ymm0
    vmovaps %ymm0, (%rdi, %rcx)
    add $32, %rcx
    jnz 1b
3:  vzeroupper
    test %rdx, %rdx
    jz 2f
    /* Do the rest of 1-byte iters, rsi = src end, rdi = dst end. */
    add %rdx, %rsi
    add %rdx, %rdi
    neg %rdx
1:  movzbl (%rsi, %rdx), %eax
    mov %al, (%rdi, %rdx)
    inc %rdx
    jnz 1b
2:  ret

/* The basic unrolled AVX memcpy: aligns dst with unrolled copies, does 64-byte copies per loop, then unrolled
   copies the rest. */
.global _naiveAvxMemcpyUnrolled
_naiveAvxMemcpyUnrolled:
    cmp $64, %rdx
    jb 3f
    /* rcx = size until the start of the 32-byte aligned dst part */
    mov %rdi, %rcx
    neg %rcx
    and $31, %rcx
    jz 2f
    sub %rcx, %rdx
    /* Copy 1, 2, 4, 8, 16 bytes depending on set bits in rcx */
    xor %r8, %r8
    test $1, %cl
    jz 1f
    movzbl (%rsi), %eax
    mov %al, (%rdi)
    inc %r8
1:  test $2, %cl
    jz 1f
    movzwl (%rsi, %r8), %eax
    mov %ax, (%rdi, %r8)
    add $2, %r8
1:  test $4, %cl
    jz 1f
    mov (%rsi, %r8), %eax
    mov %eax, (%rdi, %r8)
    add $4, %r8
1:  test $8, %cl
    jz 1f
    mov (%rsi, %r8), %rax
    mov %rax, (%rdi, %r8)
    add $8, %r8
1:  test $16, %cl
    jz 1f
    movups (%rsi, %r8), %xmm0
    movaps %xmm0, (%rdi, %r8)
    add $16, %r8
1:  add %r8, %rsi
    add %r8, %rdi
2:  mov %rdx, %rcx
    and $-64, %rcx
    jz 3f
    /* Do 64-byte iters, rsi = src end, rdi = dst end */
    and $63, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
    /* rdi is aligned, check if rsi is aligned (both movs will be aligned if it is) */
    test $63, %rsi
    jnz 2f
1:  vmovaps (%rsi, %rcx), %ymm0
    vmovaps 32(%rsi, %rcx), %ymm1
    vmovaps %ymm0, (%rdi, %rcx)
    vmovaps %ymm1, 32(%rdi, %rcx)
    add $64, %rcx
    jnz 1b
    jmp 3f
2:  vmovups (%rsi, %rcx), %ymm0
    vmovups 32(%rsi, %rcx), %ymm1
    vmovaps %ymm0, (%rdi, %rcx)
    vmovaps %ymm1, 32(%rdi, %rcx)
    add $64, %rcx
    jnz 2b
    /* Copy 32, 16, 8, 4, 2, 1 bytes depending on set bits in rdx */
3:  vzeroupper
    test %rdx, %rdx
    jz 2f
    xor %rcx, %rcx
    test $32, %rdx
    jz 1f
    vmovups (%rsi), %ymm0
    vmovups %ymm0, (%rdi)
    add $32, %rcx
    vzeroupper
1:  test $16, %dl
    jz 1f
    movups (%rsi, %rcx), %xmm0
    movups %xmm0, (%rdi, %rcx)
    add $16, %rcx
1:  test $8, %dl
    jz 1f
    mov (%rsi, %rcx), %rax
    mov %rax, (%rdi, %rcx)
    add $8, %rcx
1:  test $4, %dl
    jz 1f
    mov (%rsi, %rcx), %eax
    mov %eax, (%rdi, %rcx)
    add $4, %rcx
1:  test $2, %dl
    jz 1f
    movzwl (%rsi, %rcx), %eax
    mov %ax, (%rdi, %rcx)
    add $2, %rcx
1:  test $1, %dl
    jz 2f
    movzbl (%rsi, %rcx), %eax
    mov %al, (%rdi, %rcx)
2:  ret

/* The basic unrolled AVX memcpy: aligns dst with unrolled copies, does 64-byte copies per loop, then unrolled
   copies the rest. */
.global _naiveAvxMemcpyUnrolledV2
_naiveAvxMemcpyUnrolledV2:
    cmp $64, %rdx
    jb 3f
    /* rcx = size until the start of the 32-byte aligned dst part */
    mov %rdi, %rcx
    neg %rcx
    and $31, %rcx
    jz 2f
    sub %rcx, %rdx
    /* Copy 1, 2, 4, 8, 16 bytes depending on set bits in rcx */
    xor %r8, %r8
    test $1, %cl
    jz 1f
    movzbl (%rsi), %eax
    mov %al, (%rdi)
    inc %r8
1:  test $2, %cl
    jz 1f
    movzwl (%rsi, %r8), %eax
    mov %ax, (%rdi, %r8)
    add $2, %r8
1:  test $4, %cl
    jz 1f
    mov (%rsi, %r8), %eax
    mov %eax, (%rdi, %r8)
    add $4, %r8
1:  test $8, %cl
    jz 1f
    mov (%rsi, %r8), %rax
    mov %rax, (%rdi, %r8)
    add $8, %r8
1:  test $16, %cl
    jz 1f
    movups (%rsi, %r8), %xmm0
    movaps %xmm0, (%rdi, %r8)
    add $16, %r8
1:  add %r8, %rsi
    add %r8, %rdi
2:  mov %rdx, %rcx
    and $-64, %rcx
    jz 3f
    /* Do 64-byte iters, rsi = src end, rdi = dst end */
    and $63, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
    /* rdi is aligned, check if rsi is aligned (both movs will be aligned if it is) */
    test $63, %rsi
    jnz 2f
1:  vmovaps (%rsi, %rcx), %ymm0
    vmovaps 32(%rsi, %rcx), %ymm1
    vmovaps %ymm0, (%rdi, %rcx)
    vmovaps %ymm1, 32(%rdi, %rcx)
    add $64, %rcx
    jnz 1b
    jmp 3f
2:  vmovups (%rsi, %rcx), %ymm0
    vmovups 32(%rsi, %rcx), %ymm1
    vmovaps %ymm0, (%rdi, %rcx)
    vmovaps %ymm1, 32(%rdi, %rcx)
    add $64, %rcx
    jnz 2b
    /* Copy 32, 16, 8, 4, 2, 1 bytes depending on set bits in rdx */
3:  vzeroupper
    test %rdx, %rdx
    jz 2f
    xor %rcx, %rcx
    test $32, %rdx
    jz 1f
    vmovups (%rsi), %ymm0
    vmovups %ymm0, (%rdi)
    add $32, %rcx
    vzeroupper
1:  test $16, %dl
    jz 1f
    movups (%rsi, %rcx), %xmm0
    movups %xmm0, (%rdi, %rcx)
    add $16, %rcx
1:  test $8, %dl
    jz 1f
    mov (%rsi, %rcx), %rax
    mov %rax, (%rdi, %rcx)
    add $8, %rcx
1:  test $4, %dl
    jz 1f
    mov (%rsi, %rcx), %eax
    mov %eax, (%rdi, %rcx)
    add $4, %rcx
1:  test $2, %dl
    jz 1f
    movzwl (%rsi, %rcx), %eax
    mov %ax, (%rdi, %rcx)
    add $2, %rcx
1:  test $1, %dl
    jz 2f
    movzbl (%rsi, %rcx), %eax
    mov %al, (%rdi, %rcx)
2:  ret

.global _repMovsbMemcpy
_repMovsbMemcpy:
    mov %rdx, %rcx
    rep movsb
    ret

.global _repMovsqMemcpy
_repMovsqMemcpy:
    mov %rdx, %rcx
    shr $3, %rcx
    jz 1f
    and $7, %rdx
    rep movsq
1:  mov %rdx, %rcx
    rep movsb
    ret

/* A copy of memcpy from musl src/string/x86_64/memcpy.s */
.global _memcpyFromMusl
_memcpyFromMusl:
    mov %rdi, %rax
    cmp $8, %rdx
    jc 1f
    test $7, %edi
    jz 1f
2:  movsb
    dec %rdx
    test $7, %edi
    jnz 2b
1:  mov %rdx, %rcx
    shr $3, %rcx
    rep
    movsq
    and $7, %edx
    jz 1f
2:  movsb
    dec %edx
    jnz 2b
1:  ret

/* A simple version, dispatching to different memcpy implementations */
.global _dispatchingMemcpyHsw
_dispatchingMemcpyHsw:
    cmp $0x4000, %rdx
    jb _naiveAvxMemcpyUnrolled
    cmp $0x400000, %rdx
    jb _repMovsbMemcpy
    jmp _naiveSseMemcpyUnrolledNT


/* Copy of memcpy from Facebook folly: https://github.com/facebook/folly/blob/master/folly/memcpy.S, non-avx
   parts stripped. */
.global _folly_memcpy_short
_folly_memcpy_short:
    //        if (length == 0) return;
    test      %edx, %edx
    jz        .LEND

    movzbl    (%rsi), %ecx
    //        if (length - 4 < 0) goto LS4;
    sub       $4, %edx
    jb        .LS4

    mov       (%rsi), %ecx
    mov       (%rsi, %rdx), %edi
    mov       %ecx, (%rax)
    mov       %edi, (%rax, %rdx)
.LEND:
    rep
    ret
    nop

.LS4:
    //        At this point, length can be 1 or 2 or 3, and $cl contains
    //        the first byte.
    mov       %cl, (%rax)
    //        if (length - 4 + 2 < 0) return;
    add       $2, %edx
    jnc       .LEND

    //        length is 2 or 3 here. In either case, just copy the last
    //        two bytes.
    movzwl    (%rsi, %rdx), %ecx
    mov       %cx, (%rax, %rdx)
    ret

.align    16
.global    _folly_memcpy
_folly_memcpy:
    mov       %rdx, %rcx
    mov       %rdi, %rax
    cmp       $8, %rdx
    jb        _folly_memcpy_short

    mov       -8(%rsi, %rdx), %r8
    mov       (%rsi), %r9
    mov       %r8, -8(%rdi, %rdx)
    and       $24, %rcx
    jz        .L32

    mov       %r9, (%rdi)
    mov       %rcx, %r8
    sub       $16, %rcx
    jb        .LT32
    vmovdqu   (%rsi, %rcx), %xmm1
    vmovdqu   %xmm1, (%rdi, %rcx)
    //        Test if there are 32-byte groups
.LT32:
    add       %r8, %rsi
    and       $-32, %rdx
    jnz       .L32_adjDI
    ret

    .align    16
.L32_adjDI:
    add       %r8, %rdi
.L32:
    vmovdqu   (%rsi), %ymm0
    shr       $6, %rdx
    jnc       .L64_32read
    vmovdqu   %ymm0, (%rdi)
    lea       32(%rsi), %rsi
    jnz       .L64_adjDI
    vzeroupper
    ret

.L64_adjDI:
    add       $32, %rdi

.L64:
    vmovdqu   (%rsi), %ymm0

.L64_32read:
    vmovdqu   32(%rsi), %ymm1
    add       $64, %rsi
    vmovdqu   %ymm0, (%rdi)
    vmovdqu   %ymm1, 32(%rdi)
    add       $64, %rdi
    dec       %rdx
    jnz       .L64
    vzeroupper
    ret
