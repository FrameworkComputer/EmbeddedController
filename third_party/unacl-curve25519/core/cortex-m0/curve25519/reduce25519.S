// Implementation of a partial reduction modulo 2^255 - 38.
//
// B. Haase, Endress + Hauser Conducta GmbH & Ko. KG
// public domain.
//
// gnu assembler format.
//
// Generated and tested with C++ functions in the test subdirectory and on the target.
//

	.syntax unified
	.code	16

	.text
	.align	2

	.global	fe25519_reduceTo256Bits_asm
	.code	16
	.thumb_func
	.type	fe25519_reduceTo256Bits_asm, %function

fe25519_reduceTo256Bits_asm:
    push {r4,r5,r6,r7,r14}
    ldr r2,[r1,#60]
    lsrs r3,r2,#16
    uxth r2,r2
    movs r7,#38
    muls r2,r7
    muls r3,r7
    ldr r4,[r1,#28]
    lsrs r5,r3,#16
    lsls r3,r3,#16
    movs r6,#0
    adds r4,r4,r2
    adcs r5,r6
    adds r4,r4,r3
    adcs r5,r6
    lsls r2,r4,#1
    lsrs r2,r2,#1
    str r2,[r0,#28]
    lsrs r4,r4,#31
    lsls r5,r5,#1
    orrs r4,r5
    movs r2,#19
    muls r2,r4
    ldr r4,[r1,#0]
    adds r2,r2,r4
    movs r3,#0
    adcs r3,r6
    ldr r4,[r1,#32]
    lsrs r5,r4,#16
    uxth r4,r4
    muls r5,r7
    muls r4,r7
    adds r2,r2,r4
    adcs r3,r6
    lsls r4,r5,#16
    lsrs r5,r5,#16
    adds r2,r2,r4
    adcs r3,r5
    str r2,[r0,#0]
    ldr r4,[r1,#4]
    adds r3,r3,r4
    movs r2,#0
    adcs r2,r6
    ldr r4,[r1,#36]
    lsrs r5,r4,#16
    uxth r4,r4
    muls r5,r7
    muls r4,r7
    adds r3,r3,r4
    adcs r2,r6
    lsls r4,r5,#16
    lsrs r5,r5,#16
    adds r3,r3,r4
    adcs r2,r5
    str r3,[r0,#4]
    ldr r4,[r1,#8]
    adds r2,r2,r4
    movs r3,#0
    adcs r3,r6
    ldr r4,[r1,#40]
    lsrs r5,r4,#16
    uxth r4,r4
    muls r5,r7
    muls r4,r7
    adds r2,r2,r4
    adcs r3,r6
    lsls r4,r5,#16
    lsrs r5,r5,#16
    adds r2,r2,r4
    adcs r3,r5
    str r2,[r0,#8]
    ldr r4,[r1,#12]
    adds r3,r3,r4
    movs r2,#0
    adcs r2,r6
    ldr r4,[r1,#44]
    lsrs r5,r4,#16
    uxth r4,r4
    muls r5,r7
    muls r4,r7
    adds r3,r3,r4
    adcs r2,r6
    lsls r4,r5,#16
    lsrs r5,r5,#16
    adds r3,r3,r4
    adcs r2,r5
    str r3,[r0,#12]
    ldr r4,[r1,#16]
    adds r2,r2,r4
    movs r3,#0
    adcs r3,r6
    ldr r4,[r1,#48]
    lsrs r5,r4,#16
    uxth r4,r4
    muls r5,r7
    muls r4,r7
    adds r2,r2,r4
    adcs r3,r6
    lsls r4,r5,#16
    lsrs r5,r5,#16
    adds r2,r2,r4
    adcs r3,r5
    str r2,[r0,#16]
    ldr r4,[r1,#20]
    adds r3,r3,r4
    movs r2,#0
    adcs r2,r6
    ldr r4,[r1,#52]
    lsrs r5,r4,#16
    uxth r4,r4
    muls r5,r7
    muls r4,r7
    adds r3,r3,r4
    adcs r2,r6
    lsls r4,r5,#16
    lsrs r5,r5,#16
    adds r3,r3,r4
    adcs r2,r5
    str r3,[r0,#20]
    ldr r4,[r1,#24]
    adds r2,r2,r4
    movs r3,#0
    adcs r3,r6
    ldr r4,[r1,#56]
    lsrs r5,r4,#16
    uxth r4,r4
    muls r5,r7
    muls r4,r7
    adds r2,r2,r4
    adcs r3,r6
    lsls r4,r5,#16
    lsrs r5,r5,#16
    adds r2,r2,r4
    adcs r3,r5
    str r2,[r0,#24]
    ldr r4,[r0,#28]
    adds r4,r4,r3
    str r4,[r0,#28]
    pop {r4,r5,r6,r7,r15}

	.size	fe25519_reduceTo256Bits_asm, .-fe25519_reduceTo256Bits_asm
