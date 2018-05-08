/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Math utility functions for minute-IA */

#ifndef __CROS_EC_MATH_H
#define __CROS_EC_MATH_H

#include "config.h"

#ifdef CONFIG_FPU

#define M_PI            3.14159265358979323846
#define M_PI_2          1.57079632679489661923

static inline float sqrtf(float v)
{
	float root;

	/* root = fsqart (v); */
	asm volatile(
		"fsqrt"
		: "=t" (root)
		: "0" (v)
	);
	return root;
}

/* Absolute value of V. */
static inline float fabsf(float v)
{
	float root;

	/* root = fabs (v); */
	asm volatile(
		"fabs"
		: "=t" (root)
		: "0" (v)
	);
	return root;
}

/**
 * Natural logarithm of V.
 *
 * @return ln(v)
 */
static inline float logf(float v)
{
	float res;

	asm volatile(
		"fldln2\n"
		"fxch\n"
		"fyl2x\n"
		: "=t" (res)
		: "0" (v));

	return res;
}

/**
 * Exponential function of V.
 *
 * @return e**v
 */
static inline float expf(float v)
{
	float res;

	asm volatile(
		"fldl2e\n"
		"fmulp\n"
		"fld %%st(0)\n"
		"frndint\n"
		"fsubr %%st(0),%%st(1)\n" /* bug-binutils/19054 */
		"fxch %%st(1)\n"
		"f2xm1\n"
		"fld1\n"
		"faddp\n"
		"fscale\n"
		"fstp %%st(1)\n"
		: "=t" (res)
		: "0" (v));

	return res;
}

/**
 * X to the Y power.
 *
 * @return x**y
 */
static inline float powf(float x, float y)
{
	float res;

	asm volatile(
		"fyl2x\n"
		"fld %%st(0)\n"
		"frndint\n"
		"fsub %%st,%%st(1)\n"
		"fxch\n"
		"fchs\n"
		"f2xm1\n"
		"fld1\n"
		"faddp\n"
		"fxch\n"
		"fld1\n"
		"fscale\n"
		"fstp %%st(1)\n"
		"fmulp\n"
		: "=t" (res)
		: "0" (x), "u" (y)
		: "st(1)");

	return res;
}

/* Smallest integral value not less than V.  */
static inline float ceilf(float v)
{
	float res;
	unsigned short control_word, control_word_tmp;

	asm volatile("fnstcw %0" : "=m" (control_word));
	/* Set Rounding Mode to 10B, round up toward +infinity */
	control_word_tmp = (control_word | 0x0800) & 0xfbff;
	asm volatile(
		"fld %3\n"
		"fldcw %1\n"
		"frndint\n"
		"fldcw %2"
		: "=t" (res)
		: "m" (control_word_tmp), "m"(control_word), "m" (v));

	return res;
}

/* Arc tangent of Y/X.  */
static inline float atan2f(float y, float x)
{
	float res;

	asm volatile("fpatan" : "=t" (res) : "0" (x), "u" (y) : "st(1)");

	return res;
}

/* Arc tangent of V. */
static inline float atanf(float v)
{
	float res;

	asm volatile(
		"fld1\n"
		"fpatan\n"
		: "=t" (res)
		: "0" (v));

	return res;
}

/* Sine of V. */
static inline float sinf(float v)
{
	float res;

	asm volatile("fsin" : "=t" (res) : "0" (v));

	return res;
}

/* Cosine of V. */
static inline float cosf(float v)
{
	float res;

	asm volatile("fcos" : "=t" (res) : "0" (v));

	return res;
}

/* Arc cosine of V. */
static inline float acosf(float v)
{
	return atan2f(sqrtf(1.0 - v * v), v);
}

#define COND_FP_NAN      0x0100
#define COND_FP_SIGNBIT  0x0200
#define COND_FP_NORMAL   0x0400
#define COND_FP_ZERO     0x4000
#define COND_FP_INFINITE (COND_FP_NAN | COND_FP_NORMAL)

/* Check if V is NaN (not-a-number).  */
static inline int __isnanf(float v)
{
	uint16_t stat;

	asm volatile(
		"fxam\n"
		"fnstsw %0\n"
		: "=r" (stat)
		: "0" (v));

	return (stat & (COND_FP_NAN | COND_FP_NORMAL | COND_FP_ZERO))
	       == COND_FP_NAN;
}

/**
 * Check if V is infinite.
 *
 * @return 0 if V is finite or NaN.
 * @return +1 if V is +infinite.
 * @return -1 if V is -infinite.
 */
static inline int __isinff(float v)
{
	uint16_t stat;

	asm volatile(
		"fxam\n"
		"fnstsw %0\n"
		: "=r" (stat)
		: "v" (v));

	if ((stat & (COND_FP_NAN | COND_FP_NORMAL | COND_FP_ZERO)) ==
	    COND_FP_INFINITE) {
		/* Infinite number, check sign */
		return stat & COND_FP_SIGNBIT ? -1 : 1;
	}

	/* Finite or NaN */
	return 0;
}

#endif  /* CONFIG_FPU */
#endif  /* __CROS_EC_MATH_H */
