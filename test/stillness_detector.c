/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "motion_sense.h"
#include "stillness_detector.h"
#include "test_util.h"
#include "timer.h"

#include <stdio.h>

/*****************************************************************************/
/*
 * Need to define motion sensor globals just to compile.
 * We include motion task to force the inclusion of math_util.c
 */
struct motion_sensor_t motion_sensors[] = {};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static int test_build_still_det_struct(void)
{
	struct still_det det = STILL_DET(0.00025f, 800 * MSEC, 1200 * MSEC, 5);

	TEST_NEAR(det.var_threshold, 0.00025f, 0.000001f, "%f");
	TEST_EQ(det.min_batch_window, 800 * MSEC, "%u");
	TEST_EQ(det.max_batch_window, 1200 * MSEC, "%u");
	TEST_EQ(det.min_batch_size, 5, "%u");

	return EC_SUCCESS;
}

static int test_not_still_short_window(void)
{
	struct still_det det = STILL_DET(0.00025f, 800 * MSEC, 1200 * MSEC, 5);
	int i;

	for (i = 0; i < 6; ++i)
		TEST_ASSERT(!still_det_update(&det, i * 100 * MSEC, 0.0f, 0.0f,
					      0.0f));

	return EC_SUCCESS;
}

static int test_not_still_long_window(void)
{
	struct still_det det = STILL_DET(0.00025f, 800 * MSEC, 1200 * MSEC, 5);
	int i;

	for (i = 0; i < 5; ++i)
		TEST_ASSERT(!still_det_update(&det, i * 300 * MSEC, 0.0f, 0.0f,
					      0.0f));

	return EC_SUCCESS;
}

static int test_not_still_not_enough_samples(void)
{
	struct still_det det = STILL_DET(0.00025f, 800 * MSEC, 1200 * MSEC, 5);
	int i;

	for (i = 0; i < 4; ++i)
		TEST_ASSERT(!still_det_update(&det, i * 200 * MSEC, 0.0f, 0.0f,
					      0.0f));

	return EC_SUCCESS;
}

static int test_is_still_all_axes(void)
{
	struct still_det det = STILL_DET(0.00025f, 800 * MSEC, 1200 * MSEC, 5);
	int i;

	for (i = 0; i < 9; ++i) {
		int result = still_det_update(&det, i * 100 * MSEC, i * 0.001f,
					      i * 0.001f, i * 0.001f);

		TEST_EQ(result, i == 8 ? 1 : 0, "%d");
	}
	TEST_NEAR(det.mean_x, 0.004f, 0.0001f, "%f");
	TEST_NEAR(det.mean_y, 0.004f, 0.0001f, "%f");
	TEST_NEAR(det.mean_z, 0.004f, 0.0001f, "%f");

	return EC_SUCCESS;
}

static int test_not_still_one_axis(void)
{
	struct still_det det = STILL_DET(0.00025f, 800 * MSEC, 1200 * MSEC, 5);
	int i;

	for (i = 0; i < 9; ++i) {
		TEST_ASSERT(!still_det_update(&det, i * 100 * MSEC, i * 0.001f,
					      i * 0.001f, i * 0.01f));
	}

	return EC_SUCCESS;
}

static int test_resets(void)
{
	struct still_det det = STILL_DET(0.00025f, 800 * MSEC, 1200 * MSEC, 5);
	int i;

	for (i = 0; i < 9; ++i) {
		TEST_ASSERT(!still_det_update(&det, i * 100 * MSEC, i * 0.001f,
					      i * 0.001f, i * 0.01f));
	}

	for (i = 0; i < 9; ++i) {
		int result = still_det_update(&det, i * 100 * MSEC, i * 0.001f,
					      i * 0.001f, i * 0.001f);

		TEST_EQ(result, i == 8 ? 1 : 0, "%d");
	}
	TEST_NEAR(det.mean_x, 0.004f, 0.0001f, "%f");
	TEST_NEAR(det.mean_y, 0.004f, 0.0001f, "%f");
	TEST_NEAR(det.mean_z, 0.004f, 0.0001f, "%f");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_build_still_det_struct);
	RUN_TEST(test_not_still_short_window);
	RUN_TEST(test_not_still_long_window);
	RUN_TEST(test_not_still_not_enough_samples);
	RUN_TEST(test_is_still_all_axes);
	RUN_TEST(test_not_still_one_axis);
	RUN_TEST(test_resets);

	/* Wait for all background tasks to start. */
	crec_sleep(4);
	test_print_result();
}
