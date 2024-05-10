/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <cmsis_core.h>

/* Unfortunately, there are no FPSCR bits definitions in Zephyr.
 * TODO(b/339371023): Use the upstream definitions once it is available in
 * CMSIS.
 */
#define FPSCR_IOC BIT(0) /* Invalid operation */
#define FPSCR_DZC BIT(1) /* Division by zero */
#define FPSCR_OFC BIT(2) /* Overflow */
#define FPSCR_UFC BIT(3) /* Underflow */
#define FPSCR_IXC BIT(4) /* Inexact */
#define FPSCR_IDC BIT(7) /* Input denormal */
#define FPSCR_EXC_FLAGS \
	(FPSCR_IOC | FPSCR_DZC | FPSCR_OFC | FPSCR_UFC | FPSCR_IXC | FPSCR_IDC)

ZTEST_SUITE(cortexm_fpu, NULL, NULL, NULL, NULL, NULL);

static void reset_fpscr(void)
{
	__set_FPSCR(__get_FPSCR() & ~FPSCR_EXC_FLAGS);
}

/* Performs division without casting to double. */
static float divf(float a, float b)
{
	float result;

	__asm__ volatile("fdivs %0, %1, %2" : "=w"(result) : "w"(a), "w"(b));

	return result;
}

/* Make sure to use FPU instead of a hardcoded value computed in the build time.
 */
static inline float _sqrtf(float v)
{
	float root;

	__asm__ volatile("fsqrts %0, %1" : "=w"(root) : "w"(v));

	return root;
}

/*
 * Expect underflow when dividing the smallest number that can be represented
 * using floats.
 */
ZTEST(cortexm_fpu, test_underflow)
{
	float result;

	reset_fpscr();
	result = divf(1.40130e-45f, 2.0f);

	zassert_equal(result, 0.0f);
	zassert_true(__get_FPSCR() & FPSCR_UFC);
}

/*
 * Expect overflow when dividing the highest number that can be represented
 * using floats by number smaller than < 1.0f.
 */
ZTEST(cortexm_fpu, test_overflow)
{
	float result;

	reset_fpscr();
	result = divf(3.40282e38f, 0.5f);

	zassert_true(isinf(result));
	zassert_true(__get_FPSCR() & FPSCR_OFC);
}

/* Expect Division By Zero exception when 1.0f/0.0f. */
ZTEST(cortexm_fpu, test_division_by_zero)
{
	float result;

	reset_fpscr();
	result = divf(1.0f, 0.0f);

	zassert_true(isinf(result));
	zassert_true(__get_FPSCR() & FPSCR_DZC);
}

/* Expect Invalid Operation when trying to get square root of -1.0f. */
ZTEST(cortexm_fpu, test_invalid_operation)
{
	float result;

	reset_fpscr();
	result = _sqrtf(-1.0f);

	zassert_true(isnan(result));
	zassert_true(__get_FPSCR() & FPSCR_IOC);
}

/* Expect Inexact bit to be set when performing 2.0f/3.0f. */
ZTEST(cortexm_fpu, test_inexact)
{
	float result;

	reset_fpscr();
	result = divf(2.0f, 3.0f);

	/* Check if result is not NaN nor infinity. */
	zassert_true(!isnan(result) && !isinf(result));
	zassert_true(__get_FPSCR() & FPSCR_IXC);
}
