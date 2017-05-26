/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
