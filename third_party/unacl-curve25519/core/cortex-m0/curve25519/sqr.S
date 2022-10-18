// Author: Ana Helena Sánchez, Björn Haase (second implementation).
//
// public domain
//

 .syntax unified
 .align	2
	.global	square256_asm
	.type	square256_asm, %function
square256_asm:
// ######################
// ASM Square 256 refined karatsuba:
// ######################
 // sqr 256 Refined Karatsuba
 // pInput in r1
 // pResult in r0
 // adheres to arm eabi calling convention.
    push {r1,r4,r5,r6,r7,r14}
    mov r3,r8
    mov r4,r9
    mov r5,r10
    mov r6,r11
    mov r7,r12
    push {r3,r4,r5,r6,r7}
    mov r14,r0
    ldmia r1!,{r4,r5,r6,r7}
 // sqr 128 Refined Karatsuba
 // Input in r4 ... r7
 // Result in r0 ... r7
 // clobbers all registers except for r14
    mov r0,r4
    mov r1,r5
    subs r0,r6
    sbcs r1,r7
    sbcs r2,r2
    eors r0,r2
    eors r1,r2
    subs r0,r2
    sbcs r1,r2
    mov r8,r0
    mov r9,r1
    mov r10,r6
 // START: sqr 64 Refined Karatsuba
 // Input operands in r4,r5
 // Result in r0,r1,r2,r3
 // Clobbers: r4-r6
 // START: sqr 32
 // Input operand in r4
 // Result in r0 ,r1
 // Clobbers: r2, r3
    uxth r0,r4
    lsrs r1,r4,#16
    mov r2,r0
    muls r2,r1
    muls r0,r0
    muls r1,r1
    lsrs r3,r2,#15
    lsls r2,r2,#17
    adds r0,r0,r2
    adcs r1,r3
 // End: sqr 32
 // Result in r0 ,r1
    subs r4,r5
    sbcs r6,r6
    eors r4,r6
    subs r4,r6
 // START: sqr 32
 // Input operand in r5
 // Result in r2 ,r3
 // Clobbers: r5, r6
    uxth r2,r5
    lsrs r3,r5,#16
    mov r5,r2
    muls r5,r3
    muls r2,r2
    muls r3,r3
    lsrs r6,r5,#15
    lsls r5,r5,#17
    adds r2,r2,r5
    adcs r3,r6
 // End: sqr 32
 // Result in r2 ,r3
    movs r6,#0
    adds r2,r2,r1
    adcs r3,r6
 // START: sqr 32
 // Input operand in r4
 // Result in r4 ,r5
 // Clobbers: r1, r6
    lsrs r5,r4,#16
    uxth r4,r4
    mov r1,r4
    muls r1,r5
    muls r4,r4
    muls r5,r5
    lsrs r6,r1,#15
    lsls r1,r1,#17
    adds r4,r4,r1
    adcs r5,r6
 // End: sqr 32
 // Result in r4 ,r5
    mov r1,r2
    subs r1,r4
    sbcs r2,r5
    mov r5,r3
    movs r6,#0
    sbcs r3,r6
    adds r1,r1,r0
    adcs r2,r5
    adcs r3,r6
 // END: sqr 64 Refined Karatsuba
 // Result in r0,r1,r2,r3
 // Leaves r6 zero.
    mov r6,r10
    mov r10,r0
    mov r11,r1
    mov r12,r2
    mov r1,r3
 // START: sqr 64 Refined Karatsuba
 // Input operands in r6,r7
 // Result in r2,r3,r4,r5
 // Clobbers: r0,r7,r6
 // START: sqr 32
 // Input operand in r6
 // Result in r2 ,r3
 // Clobbers: r4, r5
    uxth r2,r6
    lsrs r3,r6,#16
    mov r4,r2
    muls r4,r3
    muls r2,r2
    muls r3,r3
    lsrs r5,r4,#15
    lsls r4,r4,#17
    adds r2,r2,r4
    adcs r3,r5
 // End: sqr 32
 // Result in r2 ,r3
    subs r6,r7
    sbcs r4,r4
    eors r6,r4
    subs r6,r4
 // START: sqr 32
 // Input operand in r7
 // Result in r4 ,r5
 // Clobbers: r0, r7
    uxth r4,r7
    lsrs r5,r7,#16
    mov r0,r4
    muls r0,r5
    muls r4,r4
    muls r5,r5
    lsrs r7,r0,#15
    lsls r0,r0,#17
    adds r4,r4,r0
    adcs r5,r7
 // End: sqr 32
 // Result in r4 ,r5
    movs r7,#0
    adds r4,r4,r3
    adcs r5,r7
 // START: sqr 32
 // Input operand in r6
 // Result in r7 ,r0
 // Clobbers: r6, r3
    uxth r7,r6
    lsrs r0,r6,#16
    mov r6,r7
    muls r6,r0
    muls r7,r7
    muls r0,r0
    lsrs r3,r6,#15
    lsls r6,r6,#17
    adds r7,r7,r6
    adcs r0,r3
 // End: sqr 32
 // Result in r7 ,r0
    mov r3,r4
    subs r3,r7
    sbcs r4,r0
    mov r0,r5
    movs r6,#0
    sbcs r5,r6
    adds r3,r3,r2
    adcs r4,r0
    adcs r5,r6
 // END: sqr 64 Refined Karatsuba
 // Result in r2,r3,r4,r5
 // Leaves r6 zero.
    mov r0,r12
    adds r2,r2,r0
    adcs r3,r1
    adcs r4,r6
    adcs r5,r6
    mov r12,r2
    mov r2,r8
    mov r8,r3
    mov r3,r9
    mov r9,r4
 // START: sqr 64 Refined Karatsuba
 // Input operands in r2,r3
 // Result in r6,r7,r0,r1
 // Clobbers: r2,r3,r4
 // START: sqr 32
 // Input operand in r2
 // Result in r6 ,r7
 // Clobbers: r0, r1
    uxth r6,r2
    lsrs r7,r2,#16
    mov r0,r6
    muls r0,r7
    muls r6,r6
    muls r7,r7
    lsrs r1,r0,#15
    lsls r0,r0,#17
    adds r6,r6,r0
    adcs r7,r1
 // End: sqr 32
 // Result in r6 ,r7
    subs r2,r3
    sbcs r4,r4
    eors r2,r4
    subs r2,r4
 // START: sqr 32
 // Input operand in r3
 // Result in r0 ,r1
 // Clobbers: r3, r4
    uxth r0,r3
    lsrs r1,r3,#16
    mov r3,r0
    muls r3,r1
    muls r0,r0
    muls r1,r1
    lsrs r4,r3,#15
    lsls r3,r3,#17
    adds r0,r0,r3
    adcs r1,r4
 // End: sqr 32
 // Result in r0 ,r1
    movs r4,#0
    adds r0,r0,r7
    adcs r1,r4
 // START: sqr 32
 // Input operand in r2
 // Result in r3 ,r4
 // Clobbers: r2, r7
    uxth r3,r2
    lsrs r4,r2,#16
    mov r2,r3
    muls r2,r4
    muls r3,r3
    muls r4,r4
    lsrs r7,r2,#15
    lsls r2,r2,#17
    adds r3,r3,r2
    adcs r4,r7
 // End: sqr 32
 // Result in r3 ,r4
    mov r7,r0
    subs r7,r3
    sbcs r0,r4
    mov r2,r1
    movs r4,#0
    sbcs r1,r4
    adds r7,r7,r6
    adcs r0,r2
    adcs r1,r4
 // END: sqr 64 Refined Karatsuba
 // Result in r6,r7,r0,r1
 // Returns r4 as zero.
    mov r2,r12
    mov r3,r8
    mov r4,r9
    subs r2,r6
    sbcs r3,r7
    mov r6,r4
    mov r7,r5
    sbcs r4,r0
    sbcs r5,r1
    movs r0,#0
    sbcs r6,r0
    sbcs r7,r0
    mov r0,r10
    adds r2,r2,r0
    mov r1,r11
    adcs r3,r1
    mov r0,r12
    adcs r4,r0
    mov r0,r8
    adcs r5,r0
    movs r0,#0
    adcs r6,r0
    adcs r7,r0
    mov r0,r10
 // END: sqr 128 Refined Karatsuba
 // Result in r0 ... r7
    push {r4,r5,r6,r7}
    mov r4,r14
    stm r4!,{r0,r1,r2,r3}
    ldr r4,[SP,#36]
    adds r4,#16
    ldm r4,{r4,r5,r6,r7}
 // sqr 128 Refined Karatsuba
 // Input in r4 ... r7
 // Result in r0 ... r7
 // clobbers all registers except for r14
    mov r0,r4
    mov r1,r5
    subs r0,r6
    sbcs r1,r7
    sbcs r2,r2
    eors r0,r2
    eors r1,r2
    subs r0,r2
    sbcs r1,r2
    mov r8,r0
    mov r9,r1
    mov r10,r6
 // START: sqr 64 Refined Karatsuba
 // Input operands in r4,r5
 // Result in r0,r1,r2,r3
 // Clobbers: r4-r6
 // START: sqr 32
 // Input operand in r4
 // Result in r0 ,r1
 // Clobbers: r2, r3
    uxth r0,r4
    lsrs r1,r4,#16
    mov r2,r0
    muls r2,r1
    muls r0,r0
    muls r1,r1
    lsrs r3,r2,#15
    lsls r2,r2,#17
    adds r0,r0,r2
    adcs r1,r3
 // End: sqr 32
 // Result in r0 ,r1
    subs r4,r5
    sbcs r6,r6
    eors r4,r6
    subs r4,r6
 // START: sqr 32
 // Input operand in r5
 // Result in r2 ,r3
 // Clobbers: r5, r6
    uxth r2,r5
    lsrs r3,r5,#16
    mov r5,r2
    muls r5,r3
    muls r2,r2
    muls r3,r3
    lsrs r6,r5,#15
    lsls r5,r5,#17
    adds r2,r2,r5
    adcs r3,r6
 // End: sqr 32
 // Result in r2 ,r3
    movs r6,#0
    adds r2,r2,r1
    adcs r3,r6
 // START: sqr 32
 // Input operand in r4
 // Result in r4 ,r5
 // Clobbers: r1, r6
    lsrs r5,r4,#16
    uxth r4,r4
    mov r1,r4
    muls r1,r5
    muls r4,r4
    muls r5,r5
    lsrs r6,r1,#15
    lsls r1,r1,#17
    adds r4,r4,r1
    adcs r5,r6
 // End: sqr 32
 // Result in r4 ,r5
    mov r1,r2
    subs r1,r4
    sbcs r2,r5
    mov r5,r3
    movs r6,#0
    sbcs r3,r6
    adds r1,r1,r0
    adcs r2,r5
    adcs r3,r6
 // END: sqr 64 Refined Karatsuba
 // Result in r0,r1,r2,r3
 // Leaves r6 zero.
    mov r6,r10
    mov r10,r0
    mov r11,r1
    mov r12,r2
    mov r1,r3
 // START: sqr 64 Refined Karatsuba
 // Input operands in r6,r7
 // Result in r2,r3,r4,r5
 // Clobbers: r0,r7,r6
 // START: sqr 32
 // Input operand in r6
 // Result in r2 ,r3
 // Clobbers: r4, r5
    uxth r2,r6
    lsrs r3,r6,#16
    mov r4,r2
    muls r4,r3
    muls r2,r2
    muls r3,r3
    lsrs r5,r4,#15
    lsls r4,r4,#17
    adds r2,r2,r4
    adcs r3,r5
 // End: sqr 32
 // Result in r2 ,r3
    subs r6,r7
    sbcs r4,r4
    eors r6,r4
    subs r6,r4
 // START: sqr 32
 // Input operand in r7
 // Result in r4 ,r5
 // Clobbers: r0, r7
    uxth r4,r7
    lsrs r5,r7,#16
    mov r0,r4
    muls r0,r5
    muls r4,r4
    muls r5,r5
    lsrs r7,r0,#15
    lsls r0,r0,#17
    adds r4,r4,r0
    adcs r5,r7
 // End: sqr 32
 // Result in r4 ,r5
    movs r7,#0
    adds r4,r4,r3
    adcs r5,r7
 // START: sqr 32
 // Input operand in r6
 // Result in r7 ,r0
 // Clobbers: r6, r3
    uxth r7,r6
    lsrs r0,r6,#16
    mov r6,r7
    muls r6,r0
    muls r7,r7
    muls r0,r0
    lsrs r3,r6,#15
    lsls r6,r6,#17
    adds r7,r7,r6
    adcs r0,r3
 // End: sqr 32
 // Result in r7 ,r0
    mov r3,r4
    subs r3,r7
    sbcs r4,r0
    mov r0,r5
    movs r6,#0
    sbcs r5,r6
    adds r3,r3,r2
    adcs r4,r0
    adcs r5,r6
 // END: sqr 64 Refined Karatsuba
 // Result in r2,r3,r4,r5
 // Leaves r6 zero.
    mov r0,r12
    adds r2,r2,r0
    adcs r3,r1
    adcs r4,r6
    adcs r5,r6
    mov r12,r2
    mov r2,r8
    mov r8,r3
    mov r3,r9
    mov r9,r4
 // START: sqr 64 Refined Karatsuba
 // Input operands in r2,r3
 // Result in r6,r7,r0,r1
 // Clobbers: r2,r3,r4
 // START: sqr 32
 // Input operand in r2
 // Result in r6 ,r7
 // Clobbers: r0, r1
    uxth r6,r2
    lsrs r7,r2,#16
    mov r0,r6
    muls r0,r7
    muls r6,r6
    muls r7,r7
    lsrs r1,r0,#15
    lsls r0,r0,#17
    adds r6,r6,r0
    adcs r7,r1
 // End: sqr 32
 // Result in r6 ,r7
    subs r2,r3
    sbcs r4,r4
    eors r2,r4
    subs r2,r4
 // START: sqr 32
 // Input operand in r3
 // Result in r0 ,r1
 // Clobbers: r3, r4
    uxth r0,r3
    lsrs r1,r3,#16
    mov r3,r0
    muls r3,r1
    muls r0,r0
    muls r1,r1
    lsrs r4,r3,#15
    lsls r3,r3,#17
    adds r0,r0,r3
    adcs r1,r4
 // End: sqr 32
 // Result in r0 ,r1
    movs r4,#0
    adds r0,r0,r7
    adcs r1,r4
 // START: sqr 32
 // Input operand in r2
 // Result in r3 ,r4
 // Clobbers: r2, r7
    uxth r3,r2
    lsrs r4,r2,#16
    mov r2,r3
    muls r2,r4
    muls r3,r3
    muls r4,r4
    lsrs r7,r2,#15
    lsls r2,r2,#17
    adds r3,r3,r2
    adcs r4,r7
 // End: sqr 32
 // Result in r3 ,r4
    mov r7,r0
    subs r7,r3
    sbcs r0,r4
    mov r2,r1
    movs r4,#0
    sbcs r1,r4
    adds r7,r7,r6
    adcs r0,r2
    adcs r1,r4
 // END: sqr 64 Refined Karatsuba
 // Result in r6,r7,r0,r1
 // Returns r4 as zero.
    mov r2,r12
    mov r3,r8
    mov r4,r9
    subs r2,r6
    sbcs r3,r7
    mov r6,r4
    mov r7,r5
    sbcs r4,r0
    sbcs r5,r1
    movs r0,#0
    sbcs r6,r0
    sbcs r7,r0
    mov r0,r10
    adds r2,r2,r0
    mov r1,r11
    adcs r3,r1
    mov r0,r12
    adcs r4,r0
    mov r0,r8
    adcs r5,r0
    movs r0,#0
    adcs r6,r0
    adcs r7,r0
    mov r0,r10
 // END: sqr 128 Refined Karatsuba
 // Result in r0 ... r7
    mov r8,r4
    mov r9,r5
    mov r10,r6
    mov r11,r7
    pop {r4,r5,r6,r7}
    adds r0,r0,r4
    adcs r1,r5
    adcs r2,r6
    adcs r3,r7
    mov r4,r8
    mov r5,r9
    mov r6,r10
    mov r7,r11
    mov r8,r0
    movs r0,#0
    adcs r4,r0
    adcs r5,r0
    adcs r6,r0
    adcs r7,r0
    mov r0,r8
    push {r0,r1,r2,r3,r4,r5,r6,r7}
    ldr r4,[SP,#52]
    ldm r4,{r0,r1,r2,r3,r4,r5,r6,r7}
    subs r4,r0
    sbcs r5,r1
    sbcs r6,r2
    sbcs r7,r3
    sbcs r0,r0
    eors r4,r0
    eors r5,r0
    eors r6,r0
    eors r7,r0
    subs r4,r0
    sbcs r5,r0
    sbcs r6,r0
    sbcs r7,r0
 // sqr 128 Refined Karatsuba
 // Input in r4 ... r7
 // Result in r0 ... r7
 // clobbers all registers except for r14
    mov r0,r4
    mov r1,r5
    subs r0,r6
    sbcs r1,r7
    sbcs r2,r2
    eors r0,r2
    eors r1,r2
    subs r0,r2
    sbcs r1,r2
    mov r8,r0
    mov r9,r1
    mov r10,r6
 // START: sqr 64 Refined Karatsuba
 // Input operands in r4,r5
 // Result in r0,r1,r2,r3
 // Clobbers: r4-r6
 // START: sqr 32
 // Input operand in r4
 // Result in r0 ,r1
 // Clobbers: r2, r3
    uxth r0,r4
    lsrs r1,r4,#16
    mov r2,r0
    muls r2,r1
    muls r0,r0
    muls r1,r1
    lsrs r3,r2,#15
    lsls r2,r2,#17
    adds r0,r0,r2
    adcs r1,r3
 // End: sqr 32
 // Result in r0 ,r1
    subs r4,r5
    sbcs r6,r6
    eors r4,r6
    subs r4,r6
 // START: sqr 32
 // Input operand in r5
 // Result in r2 ,r3
 // Clobbers: r5, r6
    uxth r2,r5
    lsrs r3,r5,#16
    mov r5,r2
    muls r5,r3
    muls r2,r2
    muls r3,r3
    lsrs r6,r5,#15
    lsls r5,r5,#17
    adds r2,r2,r5
    adcs r3,r6
 // End: sqr 32
 // Result in r2 ,r3
    movs r6,#0
    adds r2,r2,r1
    adcs r3,r6
 // START: sqr 32
 // Input operand in r4
 // Result in r4 ,r5
 // Clobbers: r1, r6
    lsrs r5,r4,#16
    uxth r4,r4
    mov r1,r4
    muls r1,r5
    muls r4,r4
    muls r5,r5
    lsrs r6,r1,#15
    lsls r1,r1,#17
    adds r4,r4,r1
    adcs r5,r6
 // End: sqr 32
 // Result in r4 ,r5
    mov r1,r2
    subs r1,r4
    sbcs r2,r5
    mov r5,r3
    movs r6,#0
    sbcs r3,r6
    adds r1,r1,r0
    adcs r2,r5
    adcs r3,r6
 // END: sqr 64 Refined Karatsuba
 // Result in r0,r1,r2,r3
 // Leaves r6 zero.
    mov r6,r10
    mov r10,r0
    mov r11,r1
    mov r12,r2
    mov r1,r3
 // START: sqr 64 Refined Karatsuba
 // Input operands in r6,r7
 // Result in r2,r3,r4,r5
 // Clobbers: r0,r7,r6
 // START: sqr 32
 // Input operand in r6
 // Result in r2 ,r3
 // Clobbers: r4, r5
    uxth r2,r6
    lsrs r3,r6,#16
    mov r4,r2
    muls r4,r3
    muls r2,r2
    muls r3,r3
    lsrs r5,r4,#15
    lsls r4,r4,#17
    adds r2,r2,r4
    adcs r3,r5
 // End: sqr 32
 // Result in r2 ,r3
    subs r6,r7
    sbcs r4,r4
    eors r6,r4
    subs r6,r4
 // START: sqr 32
 // Input operand in r7
 // Result in r4 ,r5
 // Clobbers: r0, r7
    uxth r4,r7
    lsrs r5,r7,#16
    mov r0,r4
    muls r0,r5
    muls r4,r4
    muls r5,r5
    lsrs r7,r0,#15
    lsls r0,r0,#17
    adds r4,r4,r0
    adcs r5,r7
 // End: sqr 32
 // Result in r4 ,r5
    movs r7,#0
    adds r4,r4,r3
    adcs r5,r7
 // START: sqr 32
 // Input operand in r6
 // Result in r7 ,r0
 // Clobbers: r6, r3
    uxth r7,r6
    lsrs r0,r6,#16
    mov r6,r7
    muls r6,r0
    muls r7,r7
    muls r0,r0
    lsrs r3,r6,#15
    lsls r6,r6,#17
    adds r7,r7,r6
    adcs r0,r3
 // End: sqr 32
 // Result in r7 ,r0
    mov r3,r4
    subs r3,r7
    sbcs r4,r0
    mov r0,r5
    movs r6,#0
    sbcs r5,r6
    adds r3,r3,r2
    adcs r4,r0
    adcs r5,r6
 // END: sqr 64 Refined Karatsuba
 // Result in r2,r3,r4,r5
 // Leaves r6 zero.
    mov r0,r12
    adds r2,r2,r0
    adcs r3,r1
    adcs r4,r6
    adcs r5,r6
    mov r12,r2
    mov r2,r8
    mov r8,r3
    mov r3,r9
    mov r9,r4
 // START: sqr 64 Refined Karatsuba
 // Input operands in r2,r3
 // Result in r6,r7,r0,r1
 // Clobbers: r2,r3,r4
 // START: sqr 32
 // Input operand in r2
 // Result in r6 ,r7
 // Clobbers: r0, r1
    uxth r6,r2
    lsrs r7,r2,#16
    mov r0,r6
    muls r0,r7
    muls r6,r6
    muls r7,r7
    lsrs r1,r0,#15
    lsls r0,r0,#17
    adds r6,r6,r0
    adcs r7,r1
 // End: sqr 32
 // Result in r6 ,r7
    subs r2,r3
    sbcs r4,r4
    eors r2,r4
    subs r2,r4
 // START: sqr 32
 // Input operand in r3
 // Result in r0 ,r1
 // Clobbers: r3, r4
    uxth r0,r3
    lsrs r1,r3,#16
    mov r3,r0
    muls r3,r1
    muls r0,r0
    muls r1,r1
    lsrs r4,r3,#15
    lsls r3,r3,#17
    adds r0,r0,r3
    adcs r1,r4
 // End: sqr 32
 // Result in r0 ,r1
    movs r4,#0
    adds r0,r0,r7
    adcs r1,r4
 // START: sqr 32
 // Input operand in r2
 // Result in r3 ,r4
 // Clobbers: r2, r7
    uxth r3,r2
    lsrs r4,r2,#16
    mov r2,r3
    muls r2,r4
    muls r3,r3
    muls r4,r4
    lsrs r7,r2,#15
    lsls r2,r2,#17
    adds r3,r3,r2
    adcs r4,r7
 // End: sqr 32
 // Result in r3 ,r4
    mov r7,r0
    subs r7,r3
    sbcs r0,r4
    mov r2,r1
    movs r4,#0
    sbcs r1,r4
    adds r7,r7,r6
    adcs r0,r2
    adcs r1,r4
 // END: sqr 64 Refined Karatsuba
 // Result in r6,r7,r0,r1
 // Returns r4 as zero.
    mov r2,r12
    mov r3,r8
    mov r4,r9
    subs r2,r6
    sbcs r3,r7
    mov r6,r4
    mov r7,r5
    sbcs r4,r0
    sbcs r5,r1
    movs r0,#0
    sbcs r6,r0
    sbcs r7,r0
    mov r0,r10
    adds r2,r2,r0
    mov r1,r11
    adcs r3,r1
    mov r0,r12
    adcs r4,r0
    mov r0,r8
    adcs r5,r0
    movs r0,#0
    adcs r6,r0
    adcs r7,r0
    mov r0,r10
 // END: sqr 128 Refined Karatsuba
 // Result in r0 ... r7
    mvns r0,r0
    mvns r1,r1
    mvns r2,r2
    mvns r3,r3
    mvns r4,r4
    mvns r5,r5
    mvns r6,r6
    mvns r7,r7
    mov r8,r4
    mov r9,r5
    mov r10,r6
    mov r11,r7
    movs r4,#143
    asrs r4,r4,#1
    pop {r4,r5,r6,r7}
    adcs r0,r4
    adcs r1,r5
    adcs r2,r6
    adcs r3,r7
    mov r12,r4
    movs r4,#16
    add r4,r14
    stm r4!,{r0,r1,r2,r3}
    mov r4,r12
    mov r0,r8
    adcs r0,r4
    mov r8,r0
    mov r1,r9
    adcs r1,r5
    mov r9,r1
    mov r2,r10
    adcs r2,r6
    mov r10,r2
    mov r3,r11
    adcs r3,r7
    mov r11,r3
    movs r0,#0
    adcs r0,r0
    mov r12,r0
    mov r0,r14
    ldm r0,{r0,r1,r2,r3,r4,r5,r6,r7}
    adds r0,r0,r4
    adcs r1,r5
    adcs r2,r6
    adcs r3,r7
    movs r4,#16
    add r4,r14
    stm r4!,{r0,r1,r2,r3}
    mov r14,r4
    mov r0,r13
    ldm r0!,{r4,r5,r6,r7}
    mov r1,r8
    adcs r4,r1
    mov r1,r9
    adcs r5,r1
    mov r1,r10
    adcs r6,r1
    mov r1,r11
    adcs r7,r1
    mov r0,r14
    stm r0!,{r4,r5,r6,r7}
    pop {r4,r5,r6,r7}
    mov r1,r12
    movs r2,#0
    mvns r2,r2
    adcs r1,r2
    asrs r2,r1,#4
    adds r4,r4,r1
    adcs r5,r2
    adcs r6,r2
    adcs r7,r2
    stm r0!,{r4,r5,r6,r7}
    pop {r3,r4,r5,r6,r7}
    mov r8,r3
    mov r9,r4
    mov r10,r5
    mov r11,r6
    mov r12,r7
    pop {r0,r4,r5,r6,r7,r15}
//Cycle Count ASM-Version of 256 sqr (Refined Karatsuba) (Cortex M0): 793 (697 instructions).
	.size	square256_asm, .-square256_asm
