// Implementation of multiplication of an fe25519 bit value with the curve constant 121666.
//
// B. Haase, Endress + Hauser Conducta GmbH & Ko. KG
// public domain.
//
// gnu assembler format.
//
// Generated and tested with C++ functions in the test subdirectory.
//
// ATTENTION:
// Not yet tested on target hardware.

	.syntax unified
	.code	16
	.text
	.align	2

	.global	fe25519_mpyWith121666_asm
	.code	16
	.thumb_func
	.type	fe25519_mpyWith121666_asm, %function

fe25519_mpyWith121666_asm:
    push {r4,r5,r6,r7,r14}
    ldr r7,=56130
    ldr r2,[r1,#28]
    lsls r5,r2,#16
    lsrs r6,r2,#16
    lsrs r3,r2,#16
    uxth r2,r2
    muls r2,r7
    muls r3,r7
    adds r5,r5,r2
    movs r2,#0
    adcs r6,r2
    lsls r2,r3,#16
    lsrs r3,r3,#16
    adds r5,r5,r2
    adcs r6,r3
    lsls r2,r5,#1
    lsrs r2,r2,#1
    str r2,[r0,#28]
    lsrs r5,r5,#31
    lsls r6,r6,#1
    orrs r5,r6
    movs r6,#19
    muls r5,r6
    movs r6,#0
    ldr r2,[r1,#0]
    lsls r3,r2,#16
    lsrs r4,r2,#16
    adds r5,r5,r3
    adcs r6,r4
    lsrs r3,r2,#16
    uxth r2,r2
    muls r2,r7
    muls r3,r7
    adds r5,r5,r2
    movs r2,#0
    adcs r6,r2
    lsls r2,r3,#16
    lsrs r3,r3,#16
    adds r5,r5,r2
    adcs r6,r3
    str r5,[r0,#0]
    movs r5,#0
    ldr r2,[r1,#4]
    lsls r3,r2,#16
    lsrs r4,r2,#16
    adds r6,r6,r3
    adcs r5,r4
    lsrs r3,r2,#16
    uxth r2,r2
    muls r2,r7
    muls r3,r7
    adds r6,r6,r2
    movs r2,#0
    adcs r5,r2
    lsls r2,r3,#16
    lsrs r3,r3,#16
    adds r6,r6,r2
    adcs r5,r3
    str r6,[r0,#4]
    movs r6,#0
    ldr r2,[r1,#8]
    lsls r3,r2,#16
    lsrs r4,r2,#16
    adds r5,r5,r3
    adcs r6,r4
    lsrs r3,r2,#16
    uxth r2,r2
    muls r2,r7
    muls r3,r7
    adds r5,r5,r2
    movs r2,#0
    adcs r6,r2
    lsls r2,r3,#16
    lsrs r3,r3,#16
    adds r5,r5,r2
    adcs r6,r3
    str r5,[r0,#8]
    movs r5,#0
    ldr r2,[r1,#12]
    lsls r3,r2,#16
    lsrs r4,r2,#16
    adds r6,r6,r3
    adcs r5,r4
    lsrs r3,r2,#16
    uxth r2,r2
    muls r2,r7
    muls r3,r7
    adds r6,r6,r2
    movs r2,#0
    adcs r5,r2
    lsls r2,r3,#16
    lsrs r3,r3,#16
    adds r6,r6,r2
    adcs r5,r3
    str r6,[r0,#12]
    movs r6,#0
    ldr r2,[r1,#16]
    lsls r3,r2,#16
    lsrs r4,r2,#16
    adds r5,r5,r3
    adcs r6,r4
    lsrs r3,r2,#16
    uxth r2,r2
    muls r2,r7
    muls r3,r7
    adds r5,r5,r2
    movs r2,#0
    adcs r6,r2
    lsls r2,r3,#16
    lsrs r3,r3,#16
    adds r5,r5,r2
    adcs r6,r3
    str r5,[r0,#16]
    movs r5,#0
    ldr r2,[r1,#20]
    lsls r3,r2,#16
    lsrs r4,r2,#16
    adds r6,r6,r3
    adcs r5,r4
    lsrs r3,r2,#16
    uxth r2,r2
    muls r2,r7
    muls r3,r7
    adds r6,r6,r2
    movs r2,#0
    adcs r5,r2
    lsls r2,r3,#16
    lsrs r3,r3,#16
    adds r6,r6,r2
    adcs r5,r3
    str r6,[r0,#20]
    movs r6,#0
    ldr r2,[r1,#24]
    lsls r3,r2,#16
    lsrs r4,r2,#16
    adds r5,r5,r3
    adcs r6,r4
    lsrs r3,r2,#16
    uxth r2,r2
    muls r2,r7
    muls r3,r7
    adds r5,r5,r2
    movs r2,#0
    adcs r6,r2
    lsls r2,r3,#16
    lsrs r3,r3,#16
    adds r5,r5,r2
    adcs r6,r3
    str r5,[r0,#24]
    movs r5,#0
    ldr r2,[r0,#28]
    adds r6,r6,r2
    str r6,[r0,#28]
    pop {r4,r5,r6,r7,r15}

	.size	fe25519_mpyWith121666_asm, .-fe25519_mpyWith121666_asm
