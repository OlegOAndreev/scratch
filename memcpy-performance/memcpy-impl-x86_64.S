.text

/* The easiest way to fix cross-platform differences, albeit not the fastest one */
#ifdef _WIN32

#define PROLOGUE \
	push %rsi; \
	push %rdi; \
	mov %rcx, %rdi; \
	mov %rdx, %rsi; \
	mov %r8, %rdx; \
	mov %r9, %rcx

#define EPILOGUE \
	pop %rdi; \
	pop %rsi

#else

#define PROLOGUE
#define EPILOGUE

#endif

.p2align 4
.global _isAvxSupported
_isAvxSupported:
    push %rbx
    mov $1, %rax
    cpuid
    /* Test if osxsave and avx bits are set */
    xor %rax, %rax
    and $0x18000000, %ecx
    cmp $0x18000000, %ecx
    sete %al
    pop %rbx
    ret

/* The basic unaligned memcpy: does 8-byte copies, then 1-byte copies the rest */
.p2align 4
.global _naiveMemcpy_x86_64
_naiveMemcpy_x86_64:
	PROLOGUE
    /* rcx = 8-byte aligned size, rdx = rest of size */
    mov %rdx, %rcx
    and $-8, %rcx
    jz 2f
    and $7, %rdx
    /* Do 8-byte iters, rsi = src end, rdi = dst end. */
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
    .p2align 4
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
2:  EPILOGUE
    ret

// The basic unaligned memcpy: aligns the dst with 1-byte copies, does 8-byte copies, then 1-byte copies the rest. */
.p2align 4
.global _naiveMemcpyAligned_x86_64
_naiveMemcpyAligned_x86_64:
    PROLOGUE
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
    .p2align 4
1:  mov (%rsi, %rcx), %rax
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
2:  EPILOGUE
    ret

/* The unrolled memcpy: unrolled unaligned 8-byte copies (32 per loop), then copies the rest depending on bits in remainder. */
.p2align 4
.global _naiveMemcpyUnrolled_x86_64
_naiveMemcpyUnrolled_x86_64:
    PROLOGUE
    /* rcx = 32-byte aligned size, rdx = rest of size. */
    mov %rdx, %rcx
    and $-32, %rcx
    jz 2f
    and $31, %rdx
    /* Do 32-byte iters, rsi = src end, rdi = dst end. */
    add %rcx, %rsi
    add %rcx, %rdi
    negq %rcx
    .p2align 4
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
2:  EPILOGUE
    ret

/* The basic SSE memcpy: does unaligned 16-byte copies, then 1-byte copies the rest. */
.p2align 4
.global _naiveSseMemcpy
_naiveSseMemcpy:
    PROLOGUE
    mov %rdx, %rcx
    and $-16, %rcx
    jz 3f
    /* Do 16-byte iters, rsi = src end, rdi = dst end. */
    and $15, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx
    .p2align 4
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
2:  EPILOGUE
    ret

/* The basic SSE memcpy with alignment: aligns the dst with 1-byte copies, does 16-byte copies, then 1-byte copies
   the rest. */
.p2align 4
.global _naiveSseMemcpyAligned
_naiveSseMemcpyAligned:
    PROLOGUE
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
    .p2align 4
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
2:  EPILOGUE
    ret

/* The basic SSE memcpy with unrolled body: aligns the dst with 1-byte copies, does 32-byte copies per loop, then
   1-byte copies the rest. */
.p2align 4
.global _naiveSseMemcpyUnrolledAlignedBody
_naiveSseMemcpyUnrolledAlignedBody:
    PROLOGUE
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
    .p2align 4
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
2:  EPILOGUE
    ret

/* The basic unrolled SSE memcpy: aligns dst depending on bits, does 32-byte copies per loop,
   then copies the rest depending on bits in remainder. */
.p2align 4
.global _naiveSseMemcpyUnrolledAligned
_naiveSseMemcpyUnrolledAligned:
    PROLOGUE
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
    .p2align 4
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
2:  EPILOGUE
    ret


/* The basic unrolled SSE memcpy: aligns dst with unaligned (overlapping) copies, does 32-byte copies per loop,
   then does unaligned copies of the rest. Dramatically reduces the number of branches compared to
   naiveSseMemcpyUnrolled. */
.p2align 4
.global _naiveSseMemcpyUnrolledAlignedV2
_naiveSseMemcpyUnrolledAlignedV2:
    PROLOGUE
    cmp $32, %rdx
    ja 3f

    /* Copy [17-32], [9-16], [5-8] via two pairs of movs, the 1-4 bytes via single movs with branches. */
1:  cmp $16, %dl
    jbe 1f
    /* rdx = [17-32] */
    movups (%rsi), %xmm0
    movups -16(%rsi, %rdx), %xmm1
    movups %xmm0, (%rdi)
    movups %xmm1, -16(%rdi, %rdx)
    EPILOGUE
    ret

1:  cmp $8, %dl
    jbe 1f
    /* rdx = [9-16] */
    mov (%rsi), %rax
    mov -8(%rsi, %rdx), %rcx
    mov %rax, (%rdi)
    mov %rcx, -8(%rdi, %rdx)
    EPILOGUE
    ret

1:  cmp $4, %dl
    je 1f
    jb 2f
    /* rdx = [5-8] */
    mov (%rsi), %eax
    mov -4(%rsi, %rdx), %ecx
    mov %eax, (%rdi)
    mov %ecx, -4(%rdi, %rdx)
    EPILOGUE
    ret

1:  test $4, %dl
    jz 1f
    mov (%rsi), %eax
    mov %eax, (%rdi)
    EPILOGUE
    ret

2:  test $2, %dl
    jz 1f
    movzwl -2(%rsi, %rdx), %eax
    mov %ax, -2(%rdi, %rdx)
1:  test $1, %dl
    jz 1f
    movzbl (%rsi), %eax
    mov %al, (%rdi)
1:  EPILOGUE
    ret

    /* Align the dst on 16-byte */
3:  mov %rdi, %rcx
    neg %rcx
    and $15, %rcx
    /* rcx = size until the start of the 16-byte aligned dst part */
    jz 1f

    sub %rcx, %rdx
    /* We have at least 32 bytes, copy the first 16 one (including rcx bytes). */
    movups (%rsi), %xmm0
    movups %xmm0, (%rdi)
    add %rcx, %rsi
    add %rcx, %rdi

1:  mov %rdx, %rcx
    and $-32, %rcx
    jz 3f
    /* Do 32-byte iters, rsi = end of 32-aligned src, rdi = end of 32-aligned dst, rcx = negated 32-aligned
       number of bytes, rdx = rest of bytes to copy. */
    and $31, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx

    /* Main loop */
    .p2align 4
1:  movups (%rsi, %rcx), %xmm0
    movups 16(%rsi, %rcx), %xmm1
    movaps %xmm0, (%rdi, %rcx)
    movaps %xmm1, 16(%rdi, %rcx)
    add $32, %rcx
    jnz 1b

    /* Copy the rest rdx [0-31] bytes. We have at least 32 bytes, copy in at most two movs. */
3:  test %rdx, %rdx
    jz 1f

    movups -16(%rsi, %rdx), %xmm0
    movups %xmm0, -16(%rdi, %rdx)
    cmp $16, %rdx
    jbe 1f

    movups -32(%rsi, %rdx), %xmm0
    movups %xmm0, -32(%rdi, %rdx)
1:  EPILOGUE
    ret

/* A version of naiveSseMemcpyUnrolledV2 with non-temporal stores. */
.p2align 4
.global _naiveSseMemcpyUnrolledAlignedV2NT
_naiveSseMemcpyUnrolledAlignedV2NT:
    PROLOGUE
    cmp $32, %rdx
    ja 3f

    /* Copy [17-32], [9-16], [5-8] via two pairs of movs, the 1-4 bytes via single movs with branches. */
1:  cmp $16, %dl
    jbe 1f
    /* rdx = [17-32] */
    movups (%rsi), %xmm0
    movups -16(%rsi, %rdx), %xmm1
    movups %xmm0, (%rdi)
    movups %xmm1, -16(%rdi, %rdx)
    EPILOGUE
    ret

1:  cmp $8, %dl
    jbe 1f
    /* rdx = [9-16] */
    mov (%rsi), %rax
    mov -8(%rsi, %rdx), %rcx
    mov %rax, (%rdi)
    mov %rcx, -8(%rdi, %rdx)
    EPILOGUE
    ret

1:  cmp $4, %dl
    je 1f
    jb 2f
    /* rdx = [5-8] */
    mov (%rsi), %eax
    mov -4(%rsi, %rdx), %ecx
    mov %eax, (%rdi)
    mov %ecx, -4(%rdi, %rdx)
    EPILOGUE
    ret

1:  test $4, %dl
    jz 1f
    mov (%rsi), %eax
    mov %eax, (%rdi)
    EPILOGUE
    ret

2:  test $2, %dl
    jz 1f
    movzwl -2(%rsi, %rdx), %eax
    mov %ax, -2(%rdi, %rdx)
1:  test $1, %dl
    jz 1f
    movzbl (%rsi), %eax
    mov %al, (%rdi)
1:  EPILOGUE
    ret

    /* Align the dst on 16-byte */
3:  mov %rdi, %rcx
    neg %rcx
    and $15, %rcx
    /* rcx = size until the start of the 16-byte aligned dst part */
    jz 1f

    sub %rcx, %rdx
    /* We have at least 32 bytes, copy the first 16 one (including rcx bytes). */
    movups (%rsi), %xmm0
    movups %xmm0, (%rdi)
    add %rcx, %rsi
    add %rcx, %rdi

1:  mov %rdx, %rcx
    and $-32, %rcx
    jz 3f
    /* Do 32-byte iters, rsi = end of 32-aligned src, rdi = end of 64-aligned dst, rcx = negated 32-aligned
       number of bytes, rdx = rest of bytes to copy. */
    and $31, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx

    /* Main loop */
    .p2align 4
1:  movups (%rsi, %rcx), %xmm0
    movups 16(%rsi, %rcx), %xmm1
    movntps %xmm0, (%rdi, %rcx)
    movntps %xmm1, 16(%rdi, %rcx)
    add $32, %rcx
    jnz 1b
    sfence

    /* Copy the rest rdx [0-31] bytes. We have at least 32 bytes, copy in at most two movs. */
3:  test %rdx, %rdx
    jz 1f

    movups -16(%rsi, %rdx), %xmm0
    movups %xmm0, -16(%rdi, %rdx)
    cmp $16, %rdx
    jbe 1f

    movups -32(%rsi, %rdx), %xmm0
    movups %xmm0, -32(%rdi, %rdx)
1:  EPILOGUE
    ret

/* The basic AVX memcpy: aligns the dst with 1-byte copies, does 32-byte copies, then 1-byte copies the rest. */
.p2align 4
.global _naiveAvxMemcpyAligned
_naiveAvxMemcpyAligned:
    PROLOGUE
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
    .p2align 4
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
2:  EPILOGUE
    ret

/* The basic unrolled AVX memcpy: aligns dst depending on bits, does 64-byte copies per loop,
   then copies the rest depending on bits in remainder. */
.p2align 4
.global _naiveAvxMemcpyUnrolledAligned
_naiveAvxMemcpyUnrolledAligned:
    PROLOGUE
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
    .p2align 4
1:  vmovups (%rsi, %rcx), %ymm0
    vmovups 32(%rsi, %rcx), %ymm1
    vmovaps %ymm0, (%rdi, %rcx)
    vmovaps %ymm1, 32(%rdi, %rcx)
    add $64, %rcx
    jnz 1b
    vzeroupper
    /* Copy 32, 16, 8, 4, 2, 1 bytes depending on set bits in rdx */
3:  test %rdx, %rdx
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
2:  EPILOGUE
    ret

/* The basic unrolled AVX memcpy: aligns dst with unaligned (overlapping) copies, does 64-byte copies per loop,
   then does unaligned copies of the rest. Dramatically reduces the number of branches compared to
   naiveAvxMemcpyUnrolled */
.p2align 4
.global _naiveAvxMemcpyUnrolledAlignedV2
_naiveAvxMemcpyUnrolledAlignedV2:
    PROLOGUE
    cmp $64, %rdx
    ja 3f

    /* Copy [33-64], [17-32], [9-16], [5-8] via two pairs of movs, the 1-4 bytes via single movs with branches. */
    cmp $32, %dl
    jbe 1f
    /* rdx = [33-64] */
    vmovups (%rsi), %ymm0
    vmovups -32(%rsi, %rdx), %ymm1
    vmovups %ymm0, (%rdi)
    vmovups %ymm1, -32(%rdi, %rdx)
    vzeroupper
    EPILOGUE
    ret

1:  cmp $16, %dl
    jbe 1f
    /* rdx = [17-32] */
    movups (%rsi), %xmm0
    movups -16(%rsi, %rdx), %xmm1
    movups %xmm0, (%rdi)
    movups %xmm1, -16(%rdi, %rdx)
    EPILOGUE
    ret

1:  cmp $8, %dl
    jbe 1f
    /* rdx = [9-16] */
    mov (%rsi), %rax
    mov -8(%rsi, %rdx), %rcx
    mov %rax, (%rdi)
    mov %rcx, -8(%rdi, %rdx)
    EPILOGUE
    ret

1:  cmp $4, %dl
    je 1f
    jb 2f
    /* rdx = [5-8] */
    mov (%rsi), %eax
    mov -4(%rsi, %rdx), %ecx
    mov %eax, (%rdi)
    mov %ecx, -4(%rdi, %rdx)
    EPILOGUE
    ret

1:  test $4, %dl
    jz 1f
    mov (%rsi), %eax
    mov %eax, (%rdi)
    EPILOGUE
    ret

2:  test $2, %dl
    jz 1f
    movzwl -2(%rsi, %rdx), %eax
    mov %ax, -2(%rdi, %rdx)
1:  test $1, %dl
    jz 1f
    movzbl (%rsi), %eax
    mov %al, (%rdi)
1:  EPILOGUE
    ret

    /* Align the dst on 32-byte */
3:  mov %rdi, %rcx
    neg %rcx
    and $31, %rcx
    /* rcx = size until the start of the 32-byte aligned dst part */
    jz 1f

    sub %rcx, %rdx
    /* We have at least 64 bytes, copy the first 32 one (including rcx bytes). */
    vmovups (%rsi), %ymm0
    vmovups %ymm0, (%rdi)
    add %rcx, %rsi
    add %rcx, %rdi

1:  mov %rdx, %rcx
    and $-64, %rcx
    jz 3f
    /* Do 64-byte iters, rsi = end of 64-aligned src, rdi = end of 64-aligned dst, rcx = negated 64-aligned
       number of bytes, rdx = rest of bytes to copy. */
    and $63, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx

    /* Main loop */
    .p2align 4
1:  vmovups (%rsi, %rcx), %ymm0
    vmovups 32(%rsi, %rcx), %ymm1
    vmovaps %ymm0, (%rdi, %rcx)
    vmovaps %ymm1, 32(%rdi, %rcx)
    add $64, %rcx
    jnz 1b

    /* Copy the rest rdx [0-63] bytes. We have at least 64 bytes, copy in at most two movs. */
3:  test %rdx, %rdx
    jz 1f

    vmovups -32(%rsi, %rdx), %ymm0
    vmovups %ymm0, -32(%rdi, %rdx)
    cmp $32, %rdx
    jbe 1f

    vmovups -64(%rsi, %rdx), %ymm0
    vmovups %ymm0, -64(%rdi, %rdx)
1:  vzeroupper
    EPILOGUE
    ret

/* A version of naiveAvxMemcpyUnrolledAlignedV2 with non-temporal stores. */
.p2align 4
.global _naiveAvxMemcpyUnrolledAlignedV2NT
_naiveAvxMemcpyUnrolledAlignedV2NT:
    PROLOGUE
    cmp $64, %rdx
    ja 3f

    /* Copy [33-64], [17-32], [9-16], [5-8] via two pairs of movs, the 1-4 bytes via single movs with branches. */
    cmp $32, %dl
    jbe 1f
    /* rdx = [33-64] */
    vmovups (%rsi), %ymm0
    vmovups -32(%rsi, %rdx), %ymm1
    vmovups %ymm0, (%rdi)
    vmovups %ymm1, -32(%rdi, %rdx)
    vzeroupper
    EPILOGUE
    ret

1:  cmp $16, %dl
    jbe 1f
    /* rdx = [17-32] */
    movups (%rsi), %xmm0
    movups -16(%rsi, %rdx), %xmm1
    movups %xmm0, (%rdi)
    movups %xmm1, -16(%rdi, %rdx)
    EPILOGUE
    ret

1:  cmp $8, %dl
    jbe 1f
    /* rdx = [9-16] */
    mov (%rsi), %rax
    mov -8(%rsi, %rdx), %rcx
    mov %rax, (%rdi)
    mov %rcx, -8(%rdi, %rdx)
    EPILOGUE
    ret

1:  cmp $4, %dl
    je 1f
    jb 2f
    /* rdx = [5-8] */
    mov (%rsi), %eax
    mov -4(%rsi, %rdx), %ecx
    mov %eax, (%rdi)
    mov %ecx, -4(%rdi, %rdx)
    EPILOGUE
    ret

1:  test $4, %dl
    jz 1f
    mov (%rsi), %eax
    mov %eax, (%rdi)
    EPILOGUE
    ret

2:  test $2, %dl
    jz 1f
    movzwl -2(%rsi, %rdx), %eax
    mov %ax, -2(%rdi, %rdx)
1:  test $1, %dl
    jz 1f
    movzbl (%rsi), %eax
    mov %al, (%rdi)
1:  EPILOGUE
    ret

    /* Align the dst on 32-byte */
3:  mov %rdi, %rcx
    neg %rcx
    and $31, %rcx
    /* rcx = size until the start of the 32-byte aligned dst part */
    jz 1f

    sub %rcx, %rdx
    /* We have at least 64 bytes, copy the first 32 one (including rcx bytes). */
    vmovups (%rsi), %ymm0
    vmovups %ymm0, (%rdi)
    add %rcx, %rsi
    add %rcx, %rdi

1:  mov %rdx, %rcx
    and $-64, %rcx
    jz 3f
    /* Do 64-byte iters, rsi = end of 64-aligned src, rdi = end of 64-aligned dst, rcx = negated 64-aligned
       number of bytes, rdx = rest of bytes to copy. */
    and $63, %rdx
    add %rcx, %rsi
    add %rcx, %rdi
    neg %rcx

    /* Main loop */
    .p2align 4
1:  vmovups (%rsi, %rcx), %ymm0
    vmovups 32(%rsi, %rcx), %ymm1
    vmovntps %ymm0, (%rdi, %rcx)
    vmovntps %ymm1, 32(%rdi, %rcx)
    add $64, %rcx
    jnz 1b
    sfence

    /* Copy the rest rdx [0-63] bytes. We have at least 64 bytes, copy in at most two movs. */
3:  test %rdx, %rdx
    jz 1f

    vmovups -32(%rsi, %rdx), %ymm0
    vmovups %ymm0, -32(%rdi, %rdx)
    cmp $32, %rdx
    jbe 1f

    vmovups -64(%rsi, %rdx), %ymm0
    vmovups %ymm0, -64(%rdi, %rdx)
1:  vzeroupper
    EPILOGUE
    ret

/* A simple rep movsb version */
.p2align 4
.global _repMovsbMemcpy
_repMovsbMemcpy:
    PROLOGUE
    mov %rdx, %rcx
    rep movsb
    EPILOGUE
    ret

/* A simple rep movsq + rep movsb version */
.p2align 4
.global _repMovsqMemcpy
_repMovsqMemcpy:
    PROLOGUE
    mov %rdx, %rcx
    shr $3, %rcx
    jz 1f
    and $7, %rdx
    rep movsq
1:  mov %rdx, %rcx
    rep movsb
    EPILOGUE
    ret

/* A copy of memcpy from musl src/string/x86_64/memcpy.s */
.p2align 4
.global _memcpyFromMusl_x86_64
_memcpyFromMusl_x86_64:
    PROLOGUE
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
1:  EPILOGUE
    ret

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
    EPILOGUE
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
    EPILOGUE
    ret

.align    16
.global    _folly_memcpy
_folly_memcpy:
    PROLOGUE
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
    EPILOGUE
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
    EPILOGUE
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
    EPILOGUE
    ret
