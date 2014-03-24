/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion sense code.
 */

#include <math.h>

#include "math_util.h"
#include "motion_sense.h"
#include "test_util.h"

/*****************************************************************************/
/* Mock functions */

/* Need to define accelerometer functions just to compile. */
int accel_init(enum accel_id id)
{
	return EC_SUCCESS;
}
int accel_read(enum accel_id id, int *x_acc, int *y_acc, int *z_acc)
{
	return EC_SUCCESS;
}
int accel_set_range(const enum accel_id id, const int range, const int rnd)
{
	return EC_SUCCESS;
}
int accel_get_range(const enum accel_id id, int * const range)
{
	return EC_SUCCESS;
}
int accel_set_resolution(const enum accel_id id, const int res, const int rnd)
{
	return EC_SUCCESS;
}
int accel_get_resolution(const enum accel_id id, int * const res)
{
	return EC_SUCCESS;
}
int accel_set_datarate(const enum accel_id id, const int rate, const int rnd)
{
	return EC_SUCCESS;
}
int accel_get_datarate(const enum accel_id id, int * const rate)
{
	return EC_SUCCESS;
}

/*****************************************************************************/
/* Test utilities */

/* Macro to compare two floats and check if they are equal within diff. */
#define IS_FLOAT_EQUAL(a, b, diff) ((a) >= ((b) - diff) && (a) <= ((b) + diff))

#define ACOS_TOLERANCE_DEG 0.5f
#define RAD_TO_DEG (180.0f / 3.1415926f)

static int test_acos(void)
{
	float a, b;
	float test;

	/* Test a handful of values. */
	for (test = -1.0; test <= 1.0; test += 0.01) {
		a = arc_cos(test);
		b = acos(test) * RAD_TO_DEG;
		TEST_ASSERT(IS_FLOAT_EQUAL(a, b, ACOS_TOLERANCE_DEG));
	}

	return EC_SUCCESS;
}


void run_test(void)
{
	test_reset();

	RUN_TEST(test_acos);

	test_print_result();
}
