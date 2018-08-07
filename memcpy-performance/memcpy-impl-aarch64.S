.text

/* The basic unaligned memcpy: does 8-byte copies, then 1-byte copies the rest */
.p2align 4
.global _naiveMemcpy_aarch64
_naiveMemcpy_aarch64:
    /* Subtract 8 from x2 so that we know when to end the 8-byte copy loop */
    subs x2, x2, #8
    b.lo 2f

1:  /* Do 8-byte iters. */
    ldr x4, [x1], #8
    str x4, [x0], #8
    subs x2, x2, #8
    b.hs 1b

2:  /* Compensate for the previous subs */
    adds x2, x2, #8
    cbz x2, 2f

    /* Do 1-byte iters. */
1:  ldrb w5, [x1], #1
    strb w5, [x0], #1
    subs x2, x2, #1
    b.hi 1b
2:  ret

/* A version of _naiveSseMemcpyUnrolledAligned with pairs of regular registers (via ldp/stp) and 16-byte alignment. */
.p2align 4
.global _naiveMemcpyUnrolledAligned_aarch64
_naiveMemcpyUnrolledAligned_aarch64:
    /* The main loop will copy 32 bytes per iteration */
    cmp x2, #32
    b.lo 3f

    /* Align the dst (x0) to 16-byte, x3 is the required distance to move (toAlignPtr() from memcpy-cpp-impl.cpp) */
    neg x3, x0
    and x3, x3, #15
    sub x2, x2, x3
    /* x3 < 16, check each of the bits in x3 */
    tbz x3, #0, 1f
    ldrb w4, [x1], #1
    strb w4, [x0], #1
1:  tbz x3, #1, 1f
    ldrh w4, [x1], #2
    strh w4, [x0], #2
1:  tbz x3, #2, 1f
    ldr w4, [x1], #4
    str w4, [x0], #4
1:  tbz x3, #3, 1f
    ldr x4, [x1], #8
    str x4, [x0], #8

1:  /* Check if can run at least one iteration after the aligning, if not, get to the x2 < 32 code  */
    subs x2, x2, #32
    b.lo 3f

1:  /* Do 32-byte iters, load the second 16 bytes first (is this optimal?) */
    ldp x4, x5, [x1, #16]
    ldp x6, x7, [x1], #32
    stp x4, x5, [x0, #16]
    stp x6, x7, [x0], #32
    subs x2, x2, #32
    b.hs 1b
    /* Revert x2 to non-offset state */
    adds x2, x2, #32

3:  /* Fast exit if there is nothing left */
    cbz x2, 4f
    /* x2 < 32, check each of the bits in x2 */
    tbz x2, #0, 1f
    ldrb w3, [x1], 1
    strb w3, [x0], 1
1:  tbz x2, #1, 1f
    ldrh w3, [x1], 2
    strh w3, [x0], 2
1:  tbz x2, #2, 1f
    ldr w3, [x1], 4
    str w3, [x0], 4
1:  tbz x2, #3, 1f
    ldr x3, [x1], 8
    str x3, [x0], 8
1:  tbz x2, #4, 4f
    ldp x3, x4, [x1]
    stp x3, x4, [x0]
4:  ret

/* An version of _naiveSseMemcpyUnrolledAlignedV2 with regular registers (via ldp/stp) and 16-byte alignment. */
.p2align 4
.global _naiveMemcpyUnrolledAlignedV2_aarch64
_naiveMemcpyUnrolledAlignedV2_aarch64:
    /* The main loop will copy 32 bytes per iteration */
    cmp x2, #32
    /* x3 = end of dst (x0), x4 = end of src (x1) */
    add x3, x0, x2
    add x4, x1, x2
    b.hi 3f

    /* Copy [17-32], [9-16], [5-8] via two pairs of load/stores, the 1-3 bytes via single load/stores with branches.
       Unlike, e.g. _naiveSseMemcpyUnrolledAlignedV2, it is very easy to do single instruction test and branch on ARM
       to check for 2^n <= reg < 2^(n+1), so we are using it by decrementing x2 beforehand. */
    sub x9, x2, 1
    /* Pre-check that size is not empty */
    cbz x2, 2f

    /* 17-32 */
    tbz x9, #4, 1f
    ldp x5, x6, [x1]
    ldp x7, x8, [x4, #-16]
    stp x5, x6, [x0]
    stp x7, x8, [x3, #-16]
2:  ret

1:  /* 9-16 */
    tbz x9, #3, 1f
    ldr x5, [x1]
    ldr x6, [x4, #-8]
    str x5, [x0]
    str x6, [x3, #-8]
    ret

1:  /* 5-8 */
    tbz x9, #2, 1f
    ldr w5, [x1]
    ldr w6, [x4, #-4]
    str w5, [x0]
    str w6, [x3, #-4]
    ret

1:  /* 3-4 */
    tbz x9, #1, 1f
    ldrh w5, [x1]
    ldrh w6, [x4, #-2]
    strh w5, [x0]
    strh w6, [x3, #-2]
    ret

1:  /* 1-2 */
    ldrb w5, [x1]
    ldrb w6, [x4, #-1]
    strb w5, [x0]
    strb w6, [x3, #-1]
    ret

3:  /* Align the dst (x0) to 16 bytes by simply copying the first 16 bytes (we can, because size is >= 32) */
    /* x7 is the required distance to move to align */
    neg x7, x0
    ands x7, x7, #15
    b.eq 1f
    ldp x5, x6, [x1]
    add x1, x1, x7
    sub x2, x2, x7
    stp x5, x6, [x0]
    add x0, x0, x7

1:  /* Copy the last 32 bytes beforehand, they will overlap with the previous iterations if size % 32 != 0. */
    subs x2, x2, #32
    ldp x5, x6, [x4, #-32]
    ldp x7, x8, [x4, #-16]
    b.ls 2f

1:  /* Do the main 32-byte iters */
    subs x2, x2, #32
    ldp x9, x10, [x1, #16]
    ldp x11, x12, [x1], #32
    stp x9, x10, [x0, #16]
    stp x11, x12, [x0], #32
    /* This is a hi, not hs, because we have already loaded full 32 bytes from the end */
    b.hi 1b

    /* Store the last values */
2:  stp x5, x6, [x3, #-32]
    stp x7, x8, [x3, #-16]
    ret

/* An version of _naiveMemcpyUnrolledAlignedV2_aarch64 with NEON registers and 16-byte alignment. */
.p2align 4
.global _naiveMemcpyUnrolledAlignedV2NeonRegs_aarch64
_naiveMemcpyUnrolledAlignedV2NeonRegs_aarch64:
    /* The main loop will copy 32 bytes per iteration */
    cmp x2, #32
    /* x3 = end of dst (x0), x4 = end of src (x1) */
    add x3, x0, x2
    add x4, x1, x2
    b.hi 3f

    /* Copy [17-32], [9-16], [5-8] via two pairs of load/stores, the 1-3 bytes via single load/stores with branches.
       Unlike, e.g. _naiveSseMemcpyUnrolledAlignedV2, it is very easy to do single instruction test and branch on ARM
       to check for 2^n <= reg < 2^(n+1), so we are using it by decrementing x2 beforehand. */
    sub x9, x2, 1
    /* Pre-check that size is not empty */
    cbz x2, 2f

    /* 17-32 */
    tbz x9, #4, 1f
    ldp x5, x6, [x1]
    ldp x7, x8, [x4, #-16]
    stp x5, x6, [x0]
    stp x7, x8, [x3, #-16]
2:  ret

1:  /* 9-16 */
    tbz x9, #3, 1f
    ldr x5, [x1]
    ldr x6, [x4, #-8]
    str x5, [x0]
    str x6, [x3, #-8]
    ret

1:  /* 5-8 */
    tbz x9, #2, 1f
    ldr w5, [x1]
    ldr w6, [x4, #-4]
    str w5, [x0]
    str w6, [x3, #-4]
    ret

1:  /* 3-4 */
    tbz x9, #1, 1f
    ldrh w5, [x1]
    ldrh w6, [x4, #-2]
    strh w5, [x0]
    strh w6, [x3, #-2]
    ret

1:  /* 1-2 */
    ldrb w5, [x1]
    ldrb w6, [x4, #-1]
    strb w5, [x0]
    strb w6, [x3, #-1]
    ret

3:  /* Align the dst (x0) to 32 bytes by simply copying the first 32 bytes (we can, because size is >= 32) */
    /* x7 is the required distance to move to align */
    neg x7, x0
    ands x7, x7, #31
    b.eq 1f
    ldp q0, q1, [x1]
    add x1, x1, x7
    sub x2, x2, x7
    stp q0, q1, [x0]
    add x0, x0, x7

1:  /* Copy the last 32 bytes beforehand, they will overlap with the previous iterations if size % 32 != 0. */
    subs x2, x2, #32
    ldp q2, q3, [x4, #-32]
    b.ls 2f

1:  /* Do the main 32-byte iters */
    subs x2, x2, #32
    ldp q0, q1, [x1], #32
    stp q0, q1, [x0], #32
    /* This is a hi, not hs, because we have already loaded full 32 bytes from the end */
    b.hi 1b

    /* Store the last values */
2:  stp q2, q3, [x3, #-32]
    ret

/* A version of _naiveMemcpyUnrolledAlignedV2NeonRegs_aarch64 with 64 byte iterations and 16-byte alignment. */
.p2align 4
.global _naiveMemcpyUnrolledAlignedV3NeonRegs_aarch64
_naiveMemcpyUnrolledAlignedV3NeonRegs_aarch64:
    /* The main loop will copy 64 bytes per iteration */
    cmp x2, #64
    /* x3 = end of dst (x0), x4 = end of src (x1) */
    add x3, x0, x2
    add x4, x1, x2
    b.hi 4f

    /* Copy [33-64], [17-32], [9-16], [5-8] via two pairs of load/stores, the 1-3 bytes via single load/stores with branches.
       Unlike, e.g. _naiveSseMemcpyUnrolledAlignedV2, it is very easy to do single instruction test and branch on ARM
       to check for 2^n <= reg < 2^(n+1), so we are using it by decrementing x2 beforehand. */
    sub x9, x2, 1
    /* Pre-check that size is not empty */
    cbz x2, 2f

    /* Skip first two tests if we have less than 17 bytes */
    cmp x2, #9
    b.lo 3f

    /* 32-64 */
    tbz x9, #5, 1f
    ldp x5, x6, [x1]
    ldp x7, x8, [x1, #16]
    ldp x9, x10, [x4, #-32]
    ldp x11, x12, [x4, #-16]
    stp x5, x6, [x0]
    stp x7, x8, [x0, #16]
    stp x9, x10, [x3, #-32]
    stp x11, x12, [x3, #-16]
2:  ret

    /* 17-32 */
1:  tbz x9, #4, 1f
    ldp x5, x6, [x1]
    ldp x7, x8, [x4, #-16]
    stp x5, x6, [x0]
    stp x7, x8, [x3, #-16]
    ret

1:  /* 9-16, no need to branch because we already checked that x2 >= 9 */
    ldr x5, [x1]
    ldr x6, [x4, #-8]
    str x5, [x0]
    str x6, [x3, #-8]
    ret

3:  /* 5-8 */
    tbz x9, #2, 1f
    ldr w5, [x1]
    ldr w6, [x4, #-4]
    str w5, [x0]
    str w6, [x3, #-4]
    ret

1:  /* 3-4 */
    tbz x9, #1, 1f
    ldrh w5, [x1]
    ldrh w6, [x4, #-2]
    strh w5, [x0]
    strh w6, [x3, #-2]
    ret

1:  /* 1-2 */
    ldrb w5, [x1]
    ldrb w6, [x4, #-1]
    strb w5, [x0]
    strb w6, [x3, #-1]
    ret

4:  /* Align the dst (x0) to 32 bytes by simply copying the first 32 bytes (we can, because size is >= 64) */
    /* x7 is the required distance to move to align */
    neg x7, x0
    ands x7, x7, #31
    b.eq 1f
    ldp q0, q1, [x1]
    add x1, x1, x7
    sub x2, x2, x7
    stp q0, q1, [x0]
    add x0, x0, x7

1:  /* Copy the last 64 bytes beforehand, they will overlap with the previous iterations if size % 64 != 0. */
    subs x2, x2, #64
    ldp q4, q5, [x4, #-64]
    ldp q6, q7, [x4, #-32]
    b.ls 2f

1:  /* Do the main 64-byte iters */
    subs x2, x2, #64
    ldp q0, q1, [x1, #32]
    ldp q2, q3, [x1], #64
    stp q0, q1, [x0, #32]
    stp q2, q3, [x0], #64
    /* This is a hi, not hs, because we have already loaded full 32 bytes from the end */
    b.hi 1b

    /* Store the last values */
2:  stp q4, q5, [x3, #-64]
    stp q6, q7, [x3, #-32]
    ret
