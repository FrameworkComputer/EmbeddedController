/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion_sense_fifo.
 */

#include "stdio.h"
#include "motion_sense_fifo.h"
#include "test_util.h"
#include "util.h"
#include "hwtimer.h"
#include "timer.h"
#include "accelgyro.h"
#include <sys/types.h>

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

uint32_t mkbp_last_event_time;

static struct ec_response_motion_sensor_data data[CONFIG_ACCEL_FIFO_SIZE];
static uint16_t data_bytes_read;

static int test_insert_async_event(void)
{
	int read_count;

	motion_sense_fifo_insert_async_event(motion_sensors, ASYNC_EVENT_FLUSH);
	motion_sense_fifo_insert_async_event(motion_sensors + 1,
					     ASYNC_EVENT_ODR);

	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 2, "%d");
	TEST_EQ(data_bytes_read,
		(int)(2 * sizeof(struct ec_response_motion_sensor_data)), "%d");

	TEST_BITS_SET(data[0].flags, ASYNC_EVENT_FLUSH);
	TEST_BITS_CLEARED(data[0].flags, MOTIONSENSE_SENSOR_FLAG_ODR);
	TEST_EQ(data[0].sensor_num, 0, "%d");

	TEST_BITS_SET(data[1].flags, ASYNC_EVENT_ODR);
	TEST_BITS_CLEARED(data[1].flags, MOTIONSENSE_SENSOR_FLAG_FLUSH);
	TEST_EQ(data[1].sensor_num, 1, "%d");

	return EC_SUCCESS;
}

static int test_wake_up_needed(void)
{
	data[0].flags = MOTIONSENSE_SENSOR_FLAG_WAKEUP;

	motion_sense_fifo_stage_data(data, motion_sensors, 0, 100);
	TEST_EQ(motion_sense_fifo_wake_up_needed(), 0, "%d");

	motion_sense_fifo_commit_data();
	TEST_EQ(motion_sense_fifo_wake_up_needed(), 1, "%d");

	return EC_SUCCESS;
}

static int test_wake_up_needed_overflow(void)
{
	int i;

	data[0].flags = MOTIONSENSE_SENSOR_FLAG_WAKEUP;
	motion_sense_fifo_stage_data(data, motion_sensors, 0, 100);

	data[0].flags = 0;
	/*
	 * Using CONFIG_ACCEL_FIFO_SIZE / 2 since 2 entries are inserted per
	 * 'data':
	 * - a timestamp
	 * - the data
	 */
	for (i = 0; i < (CONFIG_ACCEL_FIFO_SIZE / 2); i++)
		motion_sense_fifo_stage_data(data, motion_sensors, 0, 101 + i);

	TEST_EQ(motion_sense_fifo_wake_up_needed(), 1, "%d");
	return EC_SUCCESS;
}

static int test_adding_timestamp(void)
{
	int read_count;

	motion_sense_fifo_add_timestamp(100);
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);

	TEST_EQ(read_count, 1, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, 100, "%u");
	return EC_SUCCESS;
}

static int test_stage_data_sets_xyz(void)
{
	motion_sensors->oversampling_ratio = 1;
	motion_sensors->oversampling = 0;
	data->data[0] = 1;
	data->data[1] = 2;
	data->data[2] = 3;
	motion_sense_fifo_stage_data(data, motion_sensors, 3, 100);

	TEST_EQ(motion_sensors->xyz[0], 1, "%d");
	TEST_EQ(motion_sensors->xyz[1], 2, "%d");
	TEST_EQ(motion_sensors->xyz[2], 3, "%d");

	return EC_SUCCESS;
}

static int test_stage_data_removed_oversample(void)
{
	int read_count;

	motion_sensors->oversampling_ratio = 2;
	motion_sensors->oversampling = 0;
	data->data[0] = 1;
	data->data[1] = 2;
	data->data[2] = 3;
	motion_sense_fifo_stage_data(data, motion_sensors, 3, 100);

	data->data[0] = 4;
	data->data[1] = 5;
	data->data[2] = 6;
	motion_sense_fifo_stage_data(data, motion_sensors, 3, 110);
	motion_sense_fifo_commit_data();

	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 3, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, 100, "%u");
	TEST_BITS_CLEARED(data[1].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[1].data[0], 1, "%d");
	TEST_EQ(data[1].data[1], 2, "%d");
	TEST_EQ(data[1].data[2], 3, "%d");
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[2].timestamp, 110, "%u");

	return EC_SUCCESS;
}

static int test_stage_data_remove_all_oversampling(void)
{
	int read_count;

	motion_sensors->oversampling_ratio = 0;
	motion_sensors->oversampling = 0;
	data->data[0] = 1;
	data->data[1] = 2;
	data->data[2] = 3;
	motion_sense_fifo_stage_data(data, motion_sensors, 3, 100);

	data->data[0] = 4;
	data->data[1] = 5;
	data->data[2] = 6;
	motion_sense_fifo_stage_data(data, motion_sensors, 3, 110);
	motion_sense_fifo_commit_data();

	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 2, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, 100, "%u");
	TEST_BITS_SET(data[1].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[1].timestamp, 110, "%u");

	return EC_SUCCESS;
}

static int test_stage_data_evicts_data_with_timestamp(void)
{
	int i, read_count;

	/* Fill the fifo */
	motion_sensors->oversampling_ratio = 1;
	for (i = 0; i < CONFIG_ACCEL_FIFO_SIZE / 2; i++)
		motion_sense_fifo_stage_data(data, motion_sensors, 3, i * 100);

	/* Add a single entry (should evict 2) */
	motion_sense_fifo_add_timestamp(CONFIG_ACCEL_FIFO_SIZE * 100);
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, CONFIG_ACCEL_FIFO_SIZE - 1, "%d");
	TEST_BITS_SET(data->flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data->timestamp, 100, "%u");
	TEST_BITS_SET(data[CONFIG_ACCEL_FIFO_SIZE - 2].flags,
		      MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[CONFIG_ACCEL_FIFO_SIZE - 2].timestamp,
		CONFIG_ACCEL_FIFO_SIZE * 100, "%u");

	return EC_SUCCESS;
}

static int test_add_data_no_spreading_when_different_sensors(void)
{
	int read_count;
	uint32_t now = __hw_clock_source_read();

	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[1].oversampling_ratio = 1;

	motion_sense_fifo_stage_data(data, motion_sensors, 3, now);
	motion_sense_fifo_stage_data(data, motion_sensors + 1, 3, now);
	motion_sense_fifo_commit_data();

	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 4, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now, "%u");
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[2].timestamp, now, "%u");

	return EC_SUCCESS;
}

static int test_add_data_no_spreading_different_timestamps(void)
{
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;

	motion_sense_fifo_stage_data(data, motion_sensors, 3, 100);
	motion_sense_fifo_stage_data(data, motion_sensors, 3, 120);
	motion_sense_fifo_commit_data();

	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 4, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, 100, "%u");
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[2].timestamp, 120, "%u");

	return EC_SUCCESS;
}

static int test_spread_data_in_window(void)
{
	uint32_t now;
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[0].collection_rate = 20000; /* ns */
	now = __hw_clock_source_read();

	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 18000);
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 18000);
	motion_sense_fifo_commit_data();
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 4, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now - 18000, "%u");
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	/* TODO(b/142892004): mock __hw_clock_source_read so we can check for
	 * exact TS.
	 */
	TEST_NEAR(data[2].timestamp, now, 2, "%u");

	return EC_SUCCESS;
}

static int test_spread_data_by_collection_rate(void)
{
	const uint32_t now = __hw_clock_source_read();
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[0].collection_rate = 20000; /* ns */
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 20500);
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 20500);
	motion_sense_fifo_commit_data();
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 4, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now - 20500, "%u");
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[2].timestamp, now - 500, "%u");

	return EC_SUCCESS;
}

static int test_spread_double_commit_same_timestamp(void)
{
	const uint32_t now = __hw_clock_source_read();
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[0].collection_rate = 20000; /* ns */
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 20500);
	motion_sense_fifo_commit_data();
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 20500);
	motion_sense_fifo_commit_data();

	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 4, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now - 20500, "%u");
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_GT(time_until(now - 20500, data[2].timestamp), 10000, "%u");
	TEST_LE(time_until(now - 20500, data[2].timestamp), 20000, "%u");

	return EC_SUCCESS;
}

static int test_read_temp_on_stage(void)
{
	struct mock_read_temp_result expected = { &motion_sensors[BASE], 200,
						  EC_SUCCESS, 0, NULL };

	mock_read_temp_results = &expected;
	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[0].collection_rate = 20000; /* ns */
	motion_sense_fifo_stage_data(data, motion_sensors, 3,
				     __hw_clock_source_read() - 10000);

	TEST_EQ(expected.used_count, 1, "%d");

	return EC_SUCCESS;
}

static int test_read_temp_from_cache_on_stage(void)
{
	struct mock_read_temp_result expected = { &motion_sensors[BASE], 200,
						  EC_SUCCESS, 0, NULL };

	mock_read_temp_results = &expected;
	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[0].collection_rate = 20000; /* ns */
	motion_sense_fifo_stage_data(data, motion_sensors, 3,
				     __hw_clock_source_read() - 10000);
	motion_sense_fifo_stage_data(data, motion_sensors, 3,
				     __hw_clock_source_read() - 5000);

	TEST_EQ(expected.used_count, 1, "%d");

	return EC_SUCCESS;
}

static int test_read_temp_twice_after_cache_stale(void)
{
	struct mock_read_temp_result expected = { &motion_sensors[BASE], 200,
						  EC_SUCCESS, 0, NULL };

	mock_read_temp_results = &expected;
	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[0].collection_rate = 20000; /* ns */
	motion_sense_fifo_stage_data(data, motion_sensors, 3,
				     __hw_clock_source_read() - 10000);
	sleep(2);
	motion_sense_fifo_stage_data(data, motion_sensors, 3,
				     __hw_clock_source_read() - 5000);

	TEST_EQ(expected.used_count, 2, "%d");

	return EC_SUCCESS;
}

void before_test(void)
{
	motion_sense_fifo_commit_data();
	motion_sense_fifo_read(sizeof(data), CONFIG_ACCEL_FIFO_SIZE, &data,
			       &data_bytes_read);
	motion_sense_fifo_reset_wake_up_needed();
	memset(data, 0, sizeof(data));
	motion_sense_fifo_reset();
	mock_read_temp_results = NULL;
}

void run_test(void)
{
	test_reset();
	motion_sense_fifo_init();
	RUN_TEST(test_insert_async_event);
	RUN_TEST(test_wake_up_needed);
	RUN_TEST(test_wake_up_needed_overflow);
	RUN_TEST(test_adding_timestamp);
	RUN_TEST(test_stage_data_sets_xyz);
	RUN_TEST(test_stage_data_removed_oversample);
	RUN_TEST(test_stage_data_remove_all_oversampling);
	RUN_TEST(test_stage_data_evicts_data_with_timestamp);
	RUN_TEST(test_add_data_no_spreading_when_different_sensors);
	RUN_TEST(test_add_data_no_spreading_different_timestamps);
	RUN_TEST(test_spread_data_in_window);
	RUN_TEST(test_spread_data_by_collection_rate);
	RUN_TEST(test_spread_double_commit_same_timestamp);
	RUN_TEST(test_read_temp_on_stage);
	RUN_TEST(test_read_temp_from_cache_on_stage);
	RUN_TEST(test_read_temp_twice_after_cache_stale);

	test_print_result();
}
