/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "mag_cal.h"
#include "test_util.h"
#include <stdio.h>

/**
 * Various samples that might be seen in the wild. Normal range for magnetic
 * fields is around 80 uT. This translates to roughly +/-525 units for the
 * lis2mdl sensor.
 *
 * Random numbers were generated using the range of [518,532] (+- 2.14 uT) for
 * the high values and [-5,5] (+- 1.53 uT) for the low values.
 */
static intv3_t samples[] = {
	{ -522, 5, -5 },
	{ -528, -3, 1 },
	{ -531, -2, 0 },
	{ -525, -1, 3 },

	{ 527, 3, -2 },
	{ 523, -5, 1 },
	{ 520, -3, 2 },
	{ 522, 0, -4 },

	{ -3, -519, -2 },
	{ 1, -521, 5 },
	{ 2, -526, 4 },
	{ 0, -532, -5 },

	{ -5, 528, 4 },
	{ -2, 531, -4 },
	{ 1, 522, 2 },
	{ 5, 532, 3 },

	{ -5, 0, -524 },
	{ -1, -2, -527 },
	{ -3, 4, -532 },
	{ 5, 3, -531 },

	{ 4, -2, 524 },
	{ 1, 3, 520 },
	{ 5, -5, 528 },
	{ 0, 2, 521 },
};

static int test_mag_cal_computes_bias(void)
{
	struct mag_cal_t cal;
	int i;

	init_mag_cal(&cal);
	cal.batch_size = ARRAY_SIZE(samples);

	/* Test that we don't calibrate until we added the final sample. */
	for (i = 0; i < cal.batch_size - 1; ++i)
		TEST_EQ(0, mag_cal_update(&cal, samples[i]), "%d");
	/* Add the final sample and check calibration. */
	TEST_EQ(1, mag_cal_update(&cal, samples[cal.batch_size - 1]), "%d");
	TEST_EQ(525, FP_TO_INT(cal.radius), "%d");
	TEST_EQ(1, cal.bias[0], "%d");
	TEST_EQ(-1, cal.bias[1], "%d");
	TEST_EQ(2, cal.bias[2], "%d");

	/*
	 * State should have reset, run the same code again to verify that
	 * we get the same calibration.
	 */
	for (i = 0; i < cal.batch_size - 1; ++i)
		TEST_EQ(0, mag_cal_update(&cal, samples[i]), "%d");
	TEST_EQ(1, mag_cal_update(&cal, samples[cal.batch_size - 1]), "%d");
	TEST_EQ(525, FP_TO_INT(cal.radius), "%d");
	TEST_EQ(1, cal.bias[0], "%d");
	TEST_EQ(-1, cal.bias[1], "%d");
	TEST_EQ(2, cal.bias[2], "%d");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_mag_cal_computes_bias);

	test_print_result();
}
