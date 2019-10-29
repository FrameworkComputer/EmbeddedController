/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "online_calibration.h"
#include "test_util.h"
#include "hwtimer.h"
#include "timer.h"
#include "accelgyro.h"

struct mock_read_temp_result {
	void *s;
	int temp;
	int ret;
	int used_count;
	struct mock_read_temp_result *next;
};

static struct mock_read_temp_result *mock_read_temp_results;

static int mock_read_temp(const struct motion_sensor_t *s, int *temp)
{
	struct mock_read_temp_result *ptr = mock_read_temp_results;

	while (ptr) {
		if (ptr->s == s) {
			if (ptr->ret == EC_SUCCESS)
				*temp = ptr->temp;
			ptr->used_count++;
			return ptr->ret;
		}
		ptr = ptr->next;
	}

	return EC_ERROR_UNKNOWN;
}

static struct accelgyro_drv mock_sensor_driver = {
	.read_temp = mock_read_temp,
};

static struct accelgyro_drv empty_sensor_driver = {};

struct motion_sensor_t motion_sensors[] = {
	[BASE] = {
		.drv = &mock_sensor_driver,
	},
	[LID] = {
		.drv = &empty_sensor_driver,
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static int test_read_temp_on_stage(void)
{
	struct mock_read_temp_result expected = { &motion_sensors[BASE], 200,
						  EC_SUCCESS, 0, NULL };
	struct ec_response_motion_sensor_data data;
	int rc;

	mock_read_temp_results = &expected;
	data.sensor_num = 0;
	rc = online_calibration_process_data(
		&data, &motion_sensors[0], __hw_clock_source_read());

	TEST_EQ(rc, EC_SUCCESS, "%d");
	TEST_EQ(expected.used_count, 1, "%d");

	return EC_SUCCESS;
}

static int test_read_temp_from_cache_on_stage(void)
{
	struct mock_read_temp_result expected = { &motion_sensors[BASE], 200,
						  EC_SUCCESS, 0, NULL };
	struct ec_response_motion_sensor_data data;
	int rc;

	mock_read_temp_results = &expected;
	data.sensor_num = 0;
	rc = online_calibration_process_data(
		&data, &motion_sensors[0], __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");

	rc = online_calibration_process_data(
		&data, &motion_sensors[0], __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");

	TEST_EQ(expected.used_count, 1, "%d");

	return EC_SUCCESS;
}

static int test_read_temp_twice_after_cache_stale(void)
{
	struct mock_read_temp_result expected = { &motion_sensors[BASE], 200,
						  EC_SUCCESS, 0, NULL };
	struct ec_response_motion_sensor_data data;
	int rc;

	mock_read_temp_results = &expected;
	data.sensor_num = 0;
	rc = online_calibration_process_data(
		&data, &motion_sensors[0], __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");

	sleep(2);
	rc = online_calibration_process_data(
		&data, &motion_sensors[0], __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");

	TEST_EQ(expected.used_count, 2, "%d");

	return EC_SUCCESS;
}

void before_test(void)
{
	mock_read_temp_results = NULL;
	online_calibration_init();
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_read_temp_on_stage);
	RUN_TEST(test_read_temp_from_cache_on_stage);
	RUN_TEST(test_read_temp_twice_after_cache_stale);

	test_print_result();
}
