/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accel_cal.h"
#include "accelgyro.h"
#include "hwtimer.h"
#include "mag_cal.h"
#include "online_calibration.h"
#include "test_util.h"
#include "timer.h"

#include <stdio.h>

int mkbp_send_event(uint8_t event_type)
{
	return 1;
}

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

static struct accel_cal_algo base_accel_cal_algos[] = { {
	.newton_fit = NEWTON_FIT(4, 15, FLOAT_TO_FP(0.01f), FLOAT_TO_FP(0.25f),
				 FLOAT_TO_FP(1.0e-8f), 100),
} };

static struct accel_cal base_accel_cal_data = {
	.still_det =
		STILL_DET(FLOAT_TO_FP(0.00025f), 800 * MSEC, 1200 * MSEC, 5),
	.algos = base_accel_cal_algos,
	.num_temp_windows = ARRAY_SIZE(base_accel_cal_algos),
};

static struct mag_cal_t lid_mag_cal_data;

static bool next_accel_cal_accumulate_result;
static fpv3_t next_accel_cal_bias;

bool accel_cal_accumulate(struct accel_cal *cal, uint32_t sample_time, fp_t x,
			  fp_t y, fp_t z, fp_t temp)
{
	if (next_accel_cal_accumulate_result) {
		cal->bias[X] = next_accel_cal_bias[X];
		cal->bias[Y] = next_accel_cal_bias[Y];
		cal->bias[Z] = next_accel_cal_bias[Z];
	}
	return next_accel_cal_accumulate_result;
}

struct motion_sensor_t motion_sensors[] = {
	[BASE] = {
		.type = MOTIONSENSE_TYPE_ACCEL,
		.default_range = 4,
		.drv = &mock_sensor_driver,
		.online_calib_data[0] = {
			.type_specific_data = &base_accel_cal_data,
		},
	},
	[LID] = {
		.type = MOTIONSENSE_TYPE_MAG,
		.default_range = 4,
		.drv = &mock_sensor_driver,
		.online_calib_data[0] = {
			.type_specific_data = &lid_mag_cal_data,
		}
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
	data.sensor_num = BASE;
	rc = online_calibration_process_data(&data, &motion_sensors[0],
					     __hw_clock_source_read());

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
	data.sensor_num = BASE;
	rc = online_calibration_process_data(&data, &motion_sensors[0],
					     __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");

	rc = online_calibration_process_data(&data, &motion_sensors[0],
					     __hw_clock_source_read());
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
	data.sensor_num = BASE;
	rc = online_calibration_process_data(&data, &motion_sensors[0],
					     __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");

	crec_sleep(2);
	rc = online_calibration_process_data(&data, &motion_sensors[0],
					     __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");

	TEST_EQ(expected.used_count, 2, "%d");

	return EC_SUCCESS;
}

static int test_new_calibration_value(void)
{
	struct mock_read_temp_result expected = { &motion_sensors[BASE], 200,
						  EC_SUCCESS, 0, NULL };
	struct ec_response_motion_sensor_data data;
	struct ec_response_online_calibration_data cal_data;
	int rc;

	mock_read_temp_results = &expected;
	next_accel_cal_accumulate_result = false;
	data.sensor_num = BASE;

	rc = online_calibration_process_data(&data, &motion_sensors[BASE],
					     __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");
	TEST_EQ(online_calibration_has_new_values(), false, "%d");

	next_accel_cal_accumulate_result = true;
	next_accel_cal_bias[X] = 0.01f; /* expect:  81  */
	next_accel_cal_bias[Y] = -0.02f; /* expect: -163 */
	next_accel_cal_bias[Z] = 0; /* expect:    0 */
	rc = online_calibration_process_data(&data, &motion_sensors[BASE],
					     __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");
	TEST_EQ(online_calibration_has_new_values(), true, "%d");

	rc = online_calibration_read(&motion_sensors[BASE], &cal_data);
	TEST_EQ(rc, true, "%d");
	TEST_EQ(cal_data.data[X], 81, "%d");
	TEST_EQ(cal_data.data[Y], -163, "%d");
	TEST_EQ(cal_data.data[Z], 0, "%d");

	TEST_EQ(online_calibration_has_new_values(), false, "%d");

	return EC_SUCCESS;
}

int test_mag_reading_updated_cal(void)
{
	struct mag_cal_t expected_results;
	struct ec_response_motion_sensor_data data;
	int rc;
	int test_values[] = { 207, -17, -37 };

	data.sensor_num = LID;
	data.data[X] = test_values[X];
	data.data[Y] = test_values[Y];
	data.data[Z] = test_values[Z];

	init_mag_cal(&expected_results);
	mag_cal_update(&expected_results, test_values);

	rc = online_calibration_process_data(&data, &motion_sensors[LID],
					     __hw_clock_source_read());
	TEST_EQ(rc, EC_SUCCESS, "%d");
	TEST_EQ(expected_results.kasa_fit.nsamples,
		lid_mag_cal_data.kasa_fit.nsamples, "%d");

	return EC_SUCCESS;
}

void before_test(void)
{
	mock_read_temp_results = NULL;
	online_calibration_init();
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_read_temp_on_stage);
	RUN_TEST(test_read_temp_from_cache_on_stage);
	RUN_TEST(test_read_temp_twice_after_cache_stale);
	RUN_TEST(test_new_calibration_value);
	RUN_TEST(test_mag_reading_updated_cal);

	test_print_result();
}
