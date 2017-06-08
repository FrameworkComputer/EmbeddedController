/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
	.text
	.align	2
	.global	__gtsf2
	.type	__gtsf2, @function
__gtsf2:
	! ---------------------------------------------------------------------
	! int __gtsf2(float a, float b):
	! This function returns a value greater than zero if neither argument
	! is NaN and a is strictly greater than b.
	! ---------------------------------------------------------------------
	.global	__gesf2
	.type	__gesf2, @function
__gesf2:
	! ---------------------------------------------------------------------
	! int __gesf2(float a, float b):
	! This function returns a value greater than or equal to zero if
	! neither argument is NaN and a is greater than or equal to b.
	! ---------------------------------------------------------------------
	move	$r4, #-1
	b	.LA

	.global	__eqsf2
	.type	__eqsf2, @function
__eqsf2:
	! ---------------------------------------------------------------------
	! int __eqsf2(float a, float b):
	! This function returns zero value if neither argument is NaN,
	! and a and b are equal.
	! ---------------------------------------------------------------------
	.global	__nesf2
	.type	__nesf2, @function
__nesf2:
	! ---------------------------------------------------------------------
	! int __nesf2(float a, float b):
	! This function returns a nonzero value if either argument is NaN or if
	! a and b are unequal.
	! ---------------------------------------------------------------------
	.global	__lesf2
	.type	__lesf2, @function
__lesf2:
	! ---------------------------------------------------------------------
	! int __lesf2(float a, float b):
	! This function returns a value less than or equal to zero if neither
	! argument is NaN and a is less than b.
	! ---------------------------------------------------------------------
	.global	__ltsf2
	.type	__ltsf2, @function
__ltsf2:
	! ---------------------------------------------------------------------
	! int __ltsf2(float a, float b):
	! This function returns a value less than zero if neither argument is
	! NaN and a is strictly less than b.
	! ---------------------------------------------------------------------
	.global	__cmpsf2
	.type	__cmpsf2, @function
__cmpsf2:
	! ---------------------------------------------------------------------
	! int __cmpsf2(float a, float b);
	! This function calculates a <=> b. That is, if a is less than b, it
	! returns -1; if a if greater than b, it returns 1; and if a and b are
	! equal, it returns 0. If either argument is NaN, it returns 1, But you
	! should not rely on this; If NaN is a possibility, use higher-level
	! comparison function __unordsf2().
	! ---------------------------------------------------------------------
	move    $r4, #1

	.align	2
.LA:
	move    $r5, #0xff000000
	slli	$r2, $r0, #1
	slt	$r15, $r5, $r2
	bnez    $r15, .LMnan		! a is NaN
	slli	$r3, $r1, #1
	slt	$r15, $r5, $r3
	bnez    $r15, .LMnan		! b is NaN
	xor     $r5, $r0, $r1	        ! a and b have same sign?
	bgez    $r5, .LSameSign
.LDiffSign:
	or      $r2, $r2, $r3
	beqz	$r2, .LMequ		! 0.0f and -0.0f are equal
	move    $r2, #1			! when a==0.0f, return 1
	cmovz   $r0, $r2, $r0		! otherwise, simply return a
	ret5    $lp
.LSameSign:
	sltsi   $r15, $r0, 0		! a < 0 ?
	bnez	$r15, .LSameSignNeg
.LSameSignPos:
	! a >= 0 && b >= 0, return a - b
	sub     $r0, $r0, $r1
	ret5    $lp
.LSameSignNeg:
	! a < 0 && b < 0, return b - a
	sub     $r0, $r1, $r0
	ret5    $lp
.LMequ:
	move    $r0, #0
	ret5    $lp
.LMnan:
	move    $r0, $r4
	ret5    $lp
	.size   __cmpsf2, .-__cmpsf2
	.size   __ltsf2, .-__ltsf2
	.size   __lesf2, .-__lesf2
	.size   __nesf2, .-__nesf2
	.size   __eqsf2, .-__eqsf2
	.size   __gesf2, .-__gesf2
	.size   __gtsf2, .-__gtsf2

#define MANTA	$r0
#define EXPOA	$r1
	.text
	.align	2
	.global	__floatsisf
	.type	__floatsisf, @function
__floatsisf:
	beqz	$r0, .LKzero		! A is zero
	move	$r4, #0x80000000
	and	$r2, $r0, $r4		! sign(A)
	beqz	$r2, .LKcont
	subri	$r0, $r0, #0
	! abs(A)
.LKcont:
	move	EXPOA, #0x9e
	move	$r5, 16
	move	$r3, 0
.LKloop:
	add	$r3, $r3, $r5
	srl	$r15, MANTA, $r3
	bnez	$r15, .LKloop2
	sll	MANTA, MANTA, $r5
	sub	EXPOA, EXPOA, $r5
.LKloop2:
	srli	$r5, $r5, #1
	bnez	$r5, .LKloop
	! do rounding
	srli	$r4, $r4, #24		! 0x80
	add	MANTA, MANTA, $r4
	slt	$r15, MANTA, $r4
	add	EXPOA, EXPOA, $r15
	srai	$r4, MANTA, #8
	andi	$r4, $r4, #1
	sub	MANTA, MANTA, $r4
	slli	MANTA, MANTA, #1	! shift out implied 1
	! pack
	srli	MANTA, MANTA, #9
	slli	$r4, EXPOA, #23
	or	$r0, MANTA, $r4
	or	$r0, $r0, $r2
.LKzero:
	ret5	$lp
	.size	__floatsisf, .-__floatsisf

#undef EXPOA
#undef MANTA
#define VALUA	$r1
#define EXPOA	VALUA
#define MANTA	$r2
#define W0	$r4
#define W1	$r5
	.text
	.align	2
	.global	__fixsfsi
	.type	__fixsfsi, @function
__fixsfsi:
	slli	VALUA, $r0, #1
	slli	MANTA, VALUA, #7
	srli	EXPOA, VALUA, #24
	subri	EXPOA, EXPOA, #0x9e
	move	W1, #0x80000000
	blez	EXPOA, .LJover		! number is too big
	sltsi	$r15, EXPOA, #0x20
	beqz	$r15, .LJzero		! number is too small
	or	MANTA, MANTA, W1
	srl	MANTA, MANTA, EXPOA
	sltsi	$r15, $r0, #0
	subri	$r0, MANTA, #0
	cmovz	$r0, MANTA, $r15
	ret5	$lp
.LJzero:
	move	$r0, #0
	ret5	$lp
.LJover:
	move	W0, #0x7f800000
	slt	$r15, W0, $r0
	beqzs8	.LJnan
	move	$r0, W1
	ret5	$lp
.LJnan:
	addi	$r0, W1, -1
	ret5	$lp
	.size	__fixsfsi, .-__fixsfsi
