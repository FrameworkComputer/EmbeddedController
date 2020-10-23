/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accel_cal.h"
#include "accelgyro.h"
#include "hwtimer.h"
#include "mag_cal.h"
#include "online_calibration.h"
#include "test_util.h"
#include "gyro_cal_init_for_test.h"
#include "gyro_cal.h"
#include "timer.h"
#include <stdio.h>

int mkbp_send_event(uint8_t event_type)
{
	return 1;
}

/*
 * Mocked driver (can be re-used for all sensors).
 */

static int mock_read_temp(const struct motion_sensor_t *s, int *temp)
{
	if (temp)
		*temp = 200;
	return EC_SUCCESS;
}

static struct accelgyro_drv mock_sensor_driver = {
	.read_temp = mock_read_temp,
};

/*
 * Accelerometer, magnetometer, and gyroscope data structs.
 */

static struct accel_cal_algo accel_cal_algos[] = { {
	.newton_fit = NEWTON_FIT(4, 15, FLOAT_TO_FP(0.01f), FLOAT_TO_FP(0.25f),
				 FLOAT_TO_FP(1.0e-8f), 100),
} };

static struct accel_cal accel_cal_data = {
	.still_det =
		STILL_DET(FLOAT_TO_FP(0.00025f), 800 * MSEC, 1200 * MSEC, 5),
	.algos = accel_cal_algos,
	.num_temp_windows = ARRAY_SIZE(accel_cal_algos),
};

static struct mag_cal_t mag_cal_data;

static struct gyro_cal gyro_cal_data;

/*
 * Motion sensor array and count.
 */

struct motion_sensor_t motion_sensors[] = {
	{
		.type = MOTIONSENSE_TYPE_ACCEL,
		.default_range = 4,
		.drv = &mock_sensor_driver,
		.online_calib_data[0] = {
			.type_specific_data = &accel_cal_data,
		},
	},
	{
		.type = MOTIONSENSE_TYPE_MAG,
		.default_range = 4,
		.drv = &mock_sensor_driver,
		.online_calib_data[0] = {
			.type_specific_data = &mag_cal_data,
		},
	},
	{
		.type = MOTIONSENSE_TYPE_GYRO,
		.default_range = 4,
		.drv = &mock_sensor_driver,
		.online_calib_data[0] = {
			.type_specific_data = &gyro_cal_data,
		},
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static void spoof_sensor_data(struct motion_sensor_t *s, int x, int y, int z)
{
	struct ec_response_motion_sensor_data data;
	uint32_t timestamp = 0;

	/* Set the data and flags. */
	data.data[X] = x;
	data.data[Y] = y;
	data.data[Z] = z;
	s->flags |= MOTIONSENSE_FLAG_IN_SPOOF_MODE;

	/* Pass the data to online_calibdation. */
	online_calibration_process_data(&data, s, timestamp);
}

/*
 * Begin testing.
 */

static int test_accel_calibration_on_spoof(void)
{
	struct ec_response_online_calibration_data out;

	/* Send spoof data (1, 2, 3). */
	spoof_sensor_data(&motion_sensors[0], 1, 2, 3);

	/* Check that we have new values. */
	TEST_ASSERT(online_calibration_has_new_values());

	/* Get the new values for sensor 0. */
	TEST_ASSERT(online_calibration_read(&motion_sensors[0], &out));

	/* Validate the new values. */
	TEST_EQ(out.data[X], 1, "%d");
	TEST_EQ(out.data[Y], 2, "%d");
	TEST_EQ(out.data[Z], 3, "%d");

	/* Validate that no other sensors have data. */
	TEST_ASSERT(!online_calibration_has_new_values());

	return EC_SUCCESS;
}

static int test_mag_calibration_on_spoof(void)
{
	struct ec_response_online_calibration_data out;

	/* Send spoof data (4, 5, 6). */
	spoof_sensor_data(&motion_sensors[1], 4, 5, 6);

	/* Check that we have new values. */
	TEST_ASSERT(online_calibration_has_new_values());

	/* Get the new values for sensor 0. */
	TEST_ASSERT(online_calibration_read(&motion_sensors[1], &out));

	/* Validate the new values. */
	TEST_EQ(out.data[X], 4, "%d");
	TEST_EQ(out.data[Y], 5, "%d");
	TEST_EQ(out.data[Z], 6, "%d");

	/* Validate that no other sensors have data. */
	TEST_ASSERT(!online_calibration_has_new_values());

	return EC_SUCCESS;
}

static int test_gyro_calibration_on_spoof(void)
{
	struct ec_response_online_calibration_data out;

	/* Send spoof data (7, 8, 9). */
	spoof_sensor_data(&motion_sensors[2], 7, 8, 9);

	/* Check that we have new values. */
	TEST_ASSERT(online_calibration_has_new_values());

	/* Get the new values for sensor 0. */
	TEST_ASSERT(online_calibration_read(&motion_sensors[2], &out));

	/* Validate the new values. */
	TEST_EQ(out.data[X], 7, "%d");
	TEST_EQ(out.data[Y], 8, "%d");
	TEST_EQ(out.data[Z], 9, "%d");

	/* Validate that no other sensors have data. */
	TEST_ASSERT(!online_calibration_has_new_values());

	return EC_SUCCESS;
}

void before_test(void)
{
	online_calibration_init();
	gyro_cal_initialization_for_test(&gyro_cal_data);
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_accel_calibration_on_spoof);
	RUN_TEST(test_mag_calibration_on_spoof);
	RUN_TEST(test_gyro_calibration_on_spoof);

	test_print_result();
}
