/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion sense code.
 */

#include <math.h>
#include <stdio.h>
#include "common.h"
#include "math_util.h"
#include "motion_sense.h"
#include "test_util.h"
#include "util.h"

/*****************************************************************************/
/*
 * Need to define motion sensor globals just to compile.
 * We include motion task to force the inclusion of math_util.c
 */
struct motion_sensor_t motion_sensors[] = {};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

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
		a = FP_TO_FLOAT(arc_cos(FLOAT_TO_FP(test)));
		b = acos(test) * RAD_TO_DEG;
		TEST_ASSERT(IS_FLOAT_EQUAL(a, b, ACOS_TOLERANCE_DEG));
	}

	return EC_SUCCESS;
}


const mat33_fp_t test_matrices[] = {
	{{ 0, FLOAT_TO_FP(-1), 0},
	 {FLOAT_TO_FP(-1), 0, 0},
	 { 0, 0, FLOAT_TO_FP(1)} },
	{{ FLOAT_TO_FP(1), 0, FLOAT_TO_FP(5)},
	 { FLOAT_TO_FP(2), FLOAT_TO_FP(1), FLOAT_TO_FP(6)},
	 { FLOAT_TO_FP(3), FLOAT_TO_FP(4), 0} }
};


static int test_rotate(void)
{
	int i, j, k;
	intv3_t v = {1, 2, 3};
	intv3_t w;

	for (i = 0; i < ARRAY_SIZE(test_matrices); i++) {
		for (j = 0; j < 100; j += 10) {
			for (k = X; k <= Z; k++) {
				v[k] += j;
				v[k] %= 7;
			}

			rotate(v, test_matrices[i], w);
			rotate_inv(w, test_matrices[i], w);
			for (k = X; k <= Z; k++)
				TEST_ASSERT(v[k] == w[k]);
		}
	}
	return EC_SUCCESS;
}

test_static int test_round_divide(void)
{
	/* Check function version */
	TEST_EQ(round_divide(10, 1), 10, "%d");
	TEST_EQ(round_divide(10, 2), 5, "%d");
	TEST_EQ(round_divide(10, 3), 3, "%d");
	TEST_EQ(round_divide(10, 4), 3, "%d");
	TEST_EQ(round_divide(10, 5), 2, "%d");
	TEST_EQ(round_divide(10, 6), 2, "%d");
	TEST_EQ(round_divide(10, 7), 1, "%d");
	TEST_EQ(round_divide(10, 9), 1, "%d");
	TEST_EQ(round_divide(10, 10), 1, "%d");
	TEST_EQ(round_divide(10, 11), 1, "%d");
	TEST_EQ(round_divide(10, 20), 1, "%d");
	TEST_EQ(round_divide(10, 21), 0, "%d");

	/* Check negative conditions */
	TEST_EQ(round_divide(-10, 6), -2, "%d");
	TEST_EQ(round_divide(10, -6), -2, "%d");
	TEST_EQ(round_divide(-10, -6), 2, "%d");

	return EC_SUCCESS;
}

test_static int test_temp_conversion(void)
{
	TEST_EQ(C_TO_K(100), 373, "%d");
	TEST_EQ(K_TO_C(100), -173, "%d");

	TEST_EQ((int)CELSIUS_TO_DECI_KELVIN(100), 3732, "%d");
	TEST_EQ(DECI_KELVIN_TO_CELSIUS(100), -263, "%d");

	TEST_EQ(MILLI_KELVIN_TO_MILLI_CELSIUS(100), -273050, "%d");
	TEST_EQ(MILLI_CELSIUS_TO_MILLI_KELVIN(100), 273250, "%d");

	TEST_EQ(MILLI_KELVIN_TO_KELVIN(5000), 5, "%d");
	TEST_EQ(KELVIN_TO_MILLI_KELVIN(100), 100000, "%d");

	TEST_EQ(CELSIUS_TO_MILLI_KELVIN(100), 373150, "%d");
	TEST_EQ(MILLI_KELVIN_TO_CELSIUS(100), -273, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_acos);
	RUN_TEST(test_rotate);
	RUN_TEST(test_round_divide);
	RUN_TEST(test_temp_conversion);

	test_print_result();
}
