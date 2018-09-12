/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config_chip.h"

/*
 * DLMB register = 0x80189:
 * Disable all interrupts and switching CPU's
 * ALU (Arithmetic Logic Unit) to floating point operation mode.
 * (IEEE standard 754 floating point)
 *
 * DLMB register = 0x80009:
 * Restore interrupts and ALU.
 */
	.text
	.align	2
	.global	__addsf3
	.type	__addsf3, @function
__addsf3:
	sethi  $r2, 0x80        /* r2 = 0x80000 */
	addi   $r3, $r2, 0x189  /* r3 = 0x80189 */
	addi45 $r2, 0x9         /* r2 = 0x80009 */
	mtsr   $r3, $dlmb       /* dlmb = 0x80189 */
	dsb
	/* Floating-point addition single-precision */
	add45  $r0, $r1
	mtsr   $r2, $dlmb       /* dlmb = 0x80009 */
	dsb
	ret5   $lp
	.size	__addsf3, .-__addsf3

	.text
	.align	2
	.global	__subsf3
	.type	__subsf3, @function
__subsf3:
	sethi  $r2, 0x80        /* r2 = 0x80000 */
	addi   $r3, $r2, 0x189  /* r3 = 0x80189 */
	addi45 $r2, 0x9         /* r2 = 0x80009 */
	mtsr   $r3, $dlmb       /* dlmb = 0x80189 */
	dsb
	/* Floating-point subtraction single-precision */
	sub45  $r0, $r1
	mtsr   $r2, $dlmb       /* dlmb = 0x80009 */
	dsb
	ret5   $lp
	.size	__subsf3, .-__subsf3

	.text
	.align	2
	.global	__mulsf3
	.type	__mulsf3, @function
__mulsf3:
#ifdef IT83XX_FPU_MUL_BY_DIV
#define SIGN	$r2
#define EXPOA	$r3
#define MANTA	$r4
#define VALUA	$r5
#define EXPOB	$r6
#define MANTB	$r7
#define VALUB	$r8
#define SPROD	$r15
        /* save r6-r8 */
	smw.adm	$r6, [$sp], $r8, #0x0
	xor    SPROD, $r1, $r0  /* sign(A xor B) */
	move   SIGN, #0x80000000
	and    SPROD, SPROD, SIGN /* store sign bit */
	slli   VALUA, $r0, 1    /* A<<1, (exponent and mantissa) */
	slli   VALUB, $r1, 1    /* B<<1, (exponent and mantissa) */
	srli   EXPOA, VALUA, 24 /* exponent(A) */
	srli   EXPOB, VALUB, 24 /* exponent(B) */
	slli   MANTA, VALUA, 7  /* A<<8, mantissa(A) with exponent's LSB */
	slli   MANTB, VALUB, 7  /* A<<8, mantissa(B) with exponent's LSB */
	beqz   VALUA, .LFzeroA  /* exponent(A) and mantissa (A) are zero */
	beqc   EXPOA, 0xff, .LFinfnanA /* A is inf or NaN */
	beqz   VALUB, .LFzeroB  /* exponent(B) and mantissa (B) are zero */
	beqc   EXPOB, 0xff, .LFinfnanB /* B is inf or NaN */
        /* A*B = A/(1/B) */
	sethi  $r2, 0x80        /* r2 = 0x80000 */
	addi   $r3, $r2, 0x189  /* r3 = 0x80189 */
	addi45 $r2, 0x9         /* r2 = 0x80009 */
	mtsr   $r3, $dlmb       /* dlmb = 0x80189 */
	dsb
	sethi  $r5, #0x3f800    /* r5 = 1.0f */
	divsr  $r1,$r1,$r5,$r1  /* r1 = 1.0f / r1 */
	divsr  $r0,$r0,$r0,$r1  /* r0 = r0 / r1 */
	mtsr   $r2, $dlmb       /* dlmb = 0x80009 */
	dsb
.LFret:
	/* restore r6-r8  */
	lmw.bim	$r6, [$sp], $r8, #0x0
	ret5	$lp

.LFzeroA:                       /* A is zero */
	beqc EXPOB, 0xff, .LFnan/*zero * inf = zero * NaN = NaN */
.LFzero:
	move $r0, SPROD         /* return 0.0f or -0.0f */
	b    .LFret
.LFinfnanA:                     /* exponent(A) is 0xff */
	bne  MANTA, SIGN, .LFnan/* A is NaN: NaN * B = NaN */
	beqz VALUB, .LFnan      /* A is inf and B is zero: inf * zero = NaN */
	bnec EXPOB, 0xff, .LFinf/* B is finite: inf * B = inf */
.LFinfnanB:                     /* exponent(B) is 0xff */
	bne  MANTB, SIGN, .LFnan/* B is NaN: A * NaN = NaN */
.LFinf:
	move $r0, #0x7f800000
	or   $r0, $r0, SPROD    /* return inf or -inf */
	b   .LFret
.LFzeroB:                       /* B is zero and A is finit */
	b   .LFzero	        /* B is zero */
.LFnan:
	move $r0, #0xffc00000   /* return NaN */
	b    .LFret
#else  /* !IT83XX_FPU_MUL_BY_DIV */
	sethi  $r2, 0x80        /* r2 = 0x80000 */
	addi   $r3, $r2, 0x189  /* r3 = 0x80189 */
	addi45 $r2, 0x9         /* r2 = 0x80009 */
	mtsr   $r3, $dlmb       /* dlmb = 0x80189 */
	dsb
	/* Floating-point multiplication single-precision */
	mul33  $r0, $r1
	mtsr   $r2, $dlmb       /* dlmb = 0x80009 */
	dsb
	ret5   $lp
#endif /* IT83XX_FPU_MUL_BY_DIV */
	.size	__mulsf3, .-__mulsf3

	.text
	.align	2
	.global	__divsf3
	.type	__divsf3, @function
__divsf3:
	sethi  $r2, 0x80        /* r2 = 0x80000 */
	addi   $r3, $r2, 0x189  /* r3 = 0x80189 */
	addi45 $r2, 0x9         /* r2 = 0x80009 */
	mtsr   $r3, $dlmb       /* dlmb = 0x80189 */
	dsb
	/* Floating-point division single-precision */
	divsr  $r0,$r0,$r0,$r1
	mtsr   $r2, $dlmb       /* dlmb = 0x80009 */
	dsb
	ret5   $lp
	.size	__divsf3, .-__divsf3
