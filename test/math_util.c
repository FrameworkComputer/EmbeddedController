/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
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
