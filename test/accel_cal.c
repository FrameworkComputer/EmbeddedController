/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accel_cal.h"
#include "common.h"
#include "motion_sense.h"
#include "test_util.h"

#include <math.h>

#include <zephyr/ztest.h>

struct motion_sensor_t motion_sensors[] = {};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

struct accel_cal_algo algos[2] = {
	{
		.newton_fit = NEWTON_FIT(8, 1, 0.01f, 0.25f, 1.0e-8f, 100),
	},
	{
		.newton_fit = NEWTON_FIT(8, 1, 0.01f, 0.25f, 1.0e-8f, 100),
	}
};

struct accel_cal cal = {
	.still_det = STILL_DET(0.00025f, 800 * MSEC, 1200 * MSEC, 5),
	.algos = algos,
	.num_temp_windows = ARRAY_SIZE(algos),
};

static bool accumulate(float x, float y, float z, float temperature)
{
	return accel_cal_accumulate(&cal, 0, x, y, z, temperature) ||
	       accel_cal_accumulate(&cal, 200 * MSEC, x, y, z, temperature) ||
	       accel_cal_accumulate(&cal, 400 * MSEC, x, y, z, temperature) ||
	       accel_cal_accumulate(&cal, 600 * MSEC, x, y, z, temperature) ||
	       accel_cal_accumulate(&cal, 800 * MSEC, x, y, z, temperature) ||
	       accel_cal_accumulate(&cal, 1000 * MSEC, x, y, z, temperature);
}

static void test_accel_cal_before(const struct ztest_unit_test *test,
				  void *fixture)
{
	cal.still_det = STILL_DET(0.00025f, 800 * MSEC, 1200 * MSEC, 5);
	accel_cal_reset(&cal);
}

ZTEST(test_accel_cal, test_calibrated_correctly_with_kasa)
{
	bool has_bias;

	accumulate(1.01f, 0.01f, 0.01f, 21.0f);
	accumulate(-0.99f, 0.01f, 0.01f, 21.0f);
	accumulate(0.01f, 1.01f, 0.01f, 21.0f);
	accumulate(0.01f, -0.99f, 0.01f, 21.0f);
	accumulate(0.01f, 0.01f, 1.01f, 21.0f);
	accumulate(0.01f, 0.01f, -0.99f, 21.0f);
	accumulate(0.7171f, 0.7171f, 0.7171f, 21.0f);
	has_bias = accumulate(-0.6971f, -0.6971f, -0.6971f, 21.0f);

	zassert_true(has_bias);
	zassert_within(cal.bias[X], 0.01f, 0.0001f, "%f", cal.bias[X]);
	zassert_within(cal.bias[Y], 0.01f, 0.0001f, "%f", cal.bias[Y]);
	zassert_within(cal.bias[Z], 0.01f, 0.0001f, "%f", cal.bias[Z]);
}

ZTEST(test_accel_cal, test_calibrated_correctly_with_newton)
{
	bool has_bias = false;
	struct kasa_fit kasa;
	fpv3_t kasa_bias;
	float kasa_radius;
	int i;
	float data[] = {
		1.00290f, 0.09170f, 0.09649f, 0.95183f, 0.23626f, 0.25853f,
		0.95023f, 0.15387f, 0.31865f, 0.97374f, 0.01639f, 0.27675f,
		0.88521f, 0.30212f, 0.39558f, 0.92787f, 0.35157f, 0.21209f,
		0.95162f, 0.33173f, 0.10924f, 0.98397f, 0.22644f, 0.07737f,
	};

	kasa_reset(&kasa);
	for (i = 0; i < ARRAY_SIZE(data); i += 3) {
		zassert_false(has_bias);
		kasa_accumulate(&kasa, data[i], data[i + 1], data[i + 2]);
		has_bias = accumulate(data[i], data[i + 1], data[i + 2], 21.0f);
	}

	kasa_compute(&kasa, kasa_bias, &kasa_radius);
	zassert_true(has_bias);
	/* Check that the bias is right */
	zassert_within(cal.bias[X], 0.01f, 0.001f, "%f", cal.bias[X]);
	zassert_within(cal.bias[Y], 0.01f, 0.001f, "%f", cal.bias[Y]);
	zassert_within(cal.bias[Z], 0.01f, 0.001f, "%f", cal.bias[Z]);
	/* Demonstrate that we got a better bias compared to kasa */
	zassert_true(sqrtf(powf(cal.bias[X] - 0.01f, 2.0f) +
			   powf(cal.bias[Y] - 0.01f, 2.0f) +
			   powf(cal.bias[Z] - 0.01f, 2.0f)) <
			     sqrtf(powf(kasa_bias[X] - 0.01f, 2.0f) +
				   powf(kasa_bias[Y] - 0.01f, 2.0f) +
				   powf(kasa_bias[Z] - 0.01f, 2.0f)),
		     NULL);
}

ZTEST(test_accel_cal, test_temperature_gates)
{
	bool has_bias;

	accumulate(1.01f, 0.01f, 0.01f, 21.0f);
	accumulate(-0.99f, 0.01f, 0.01f, 21.0f);
	accumulate(0.01f, 1.01f, 0.01f, 21.0f);
	accumulate(0.01f, -0.99f, 0.01f, 21.0f);
	accumulate(0.01f, 0.01f, 1.01f, 21.0f);
	accumulate(0.01f, 0.01f, -0.99f, 21.0f);
	accumulate(0.7171f, 0.7171f, 0.7171f, 21.0f);
	has_bias = accumulate(-0.6971f, -0.6971f, -0.6971f, 31.0f);

	zassert_false(has_bias);
}

ZTEST_SUITE(test_accel_cal, NULL, NULL, NULL, test_accel_cal_before, NULL);
