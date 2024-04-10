/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion_sense_fifo.
 */

#include "accelgyro.h"
#include "ec_commands.h"
#include "hwtimer.h"
#include "motion_sense_fifo.h"
#include "stdio.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#include <sys/types.h>

mutex_t g_sensor_mutex;

struct motion_sensor_t motion_sensors[] = {
	[BASE] = {},
	[LID] = {},
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
	uint8_t fifo_info_buffer
		[sizeof(struct ec_response_motion_sense_fifo_info) +
		 sizeof(uint16_t) * MAX_MOTION_SENSORS];
	struct ec_response_motion_sense_fifo_info *fifo_info =
		(void *)fifo_info_buffer;
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

	/*
	 * Check that count is 0 and total_lost is 0, oversampling should be
	 * removing the data before it touches the FIFO.
	 */
	motion_sense_fifo_get_info(fifo_info, /*reset=*/false);
	TEST_EQ(fifo_info->count, 0, "%d");
	TEST_EQ(fifo_info->total_lost, 0, "%d");

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
	uint8_t fifo_info_buffer
		[sizeof(struct ec_response_motion_sense_fifo_info) +
		 sizeof(uint16_t) * MAX_MOTION_SENSORS];
	struct ec_response_motion_sense_fifo_info *fifo_info =
		(void *)fifo_info_buffer;
	int i, read_count;

	/* Fill the fifo */
	motion_sensors->oversampling_ratio = 1;
	for (i = 0; i < CONFIG_ACCEL_FIFO_SIZE / 2; i++)
		motion_sense_fifo_stage_data(data, motion_sensors, 3, i * 100);

	/* Add a single entry (should evict 2) */
	motion_sense_fifo_add_timestamp(CONFIG_ACCEL_FIFO_SIZE * 100);

	/*
	 * Check that count is 1 smaller than the total size and total_lost is 2
	 * because 2 entries were evicted together.
	 */
	motion_sense_fifo_get_info(fifo_info, /*reset=*/false);
	TEST_EQ(fifo_info->count, CONFIG_ACCEL_FIFO_SIZE - 1, "%d");
	TEST_EQ(fifo_info->total_lost, 2, "%d");

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
	motion_sense_set_data_period(0, 20 /* us */);
	now = __hw_clock_source_read();

	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 18);
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 18);
	motion_sense_fifo_commit_data();
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 4, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now - 18, "%u");
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	/* TODO(b/142892004): mock __hw_clock_source_read so we can check for
	 * exact TS.
	 */
	TEST_NEAR(data[2].timestamp, now, 2, "%u");

	return EC_SUCCESS;
}

static int test_spread_data_on_overflow(void)
{
	const uint32_t now = __hw_clock_source_read();
	const int fill_count = (CONFIG_ACCEL_FIFO_SIZE / 2) - 1;
	int i, read_count;

	/* Set up the sensors */
	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[1].oversampling_ratio = 1;
	motion_sense_set_data_period(0, 20 /* us */);

	/* Add 1 sample for sensor [1]. This will be evicted. */
	data->sensor_num = 1;
	motion_sense_fifo_stage_data(data, motion_sensors + 1, 3, 0);

	/*
	 * Fill the rest of the fifo, every 2 entries will have the same
	 * timestamp simulating have 2 entries on the hardware FIFO per read.
	 */
	data->sensor_num = 0;
	for (i = 0; i < fill_count; i++) {
		int ts = now - ((fill_count - i) / 2) * 10;

		motion_sense_fifo_stage_data(data, motion_sensors, 3, ts);
	}

	/* Insert an async event which also causes a commit */
	motion_sense_fifo_insert_async_event(motion_sensors, ASYNC_EVENT_FLUSH);

	read_count =
		motion_sense_fifo_read(sizeof(data), 4, data, &data_bytes_read);

	/* Verify that we read 4 entries */
	TEST_EQ(read_count, 4, "%d");

	/* Verify that entries 0 and 2 are timestamps (1 and 3 are data) */
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);

	/*
	 * Verify that the first read entry is the first one added in the for
	 * loop above.
	 */
	TEST_EQ(data[0].sensor_num, 0, "%u");
	TEST_EQ(data[0].timestamp, now - ((fill_count - 1) / 2) * 10, "%u");

	/* Verify that the timestamp was spread */
	TEST_NE(data[0].timestamp, data[2].timestamp, "%u");

	return EC_SUCCESS;
}

static int test_spread_data_by_collection_rate(void)
{
	const uint32_t now = __hw_clock_source_read();
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sense_set_data_period(0, 20 /* us */);
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 25);
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 25);
	motion_sense_fifo_commit_data();
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 4, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now - 25, "%u");
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[2].timestamp, now - 5, "%u");

	return EC_SUCCESS;
}

static int test_commit_non_data_or_timestamp_entries(void)
{
	const uint32_t now = __hw_clock_source_read();
	int read_count;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sense_set_data_period(0, 20 /* us */);

	/* Insert non-data entry */
	data[0].flags = MOTIONSENSE_SENSOR_FLAG_ODR;
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 25);

	/* Insert data entry */
	data[0].flags = 0;
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now - 25);

	motion_sense_fifo_commit_data();
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 4, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now - 25, "%u");
	TEST_BITS_SET(data[1].flags, MOTIONSENSE_SENSOR_FLAG_ODR);
	TEST_BITS_SET(data[2].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[2].timestamp, now - 25, "%u");

	return EC_SUCCESS;
}

static int test_get_info_size(void)
{
	uint8_t fifo_info_buffer
		[sizeof(struct ec_response_motion_sense_fifo_info) +
		 sizeof(uint16_t) * MAX_MOTION_SENSORS];
	struct ec_response_motion_sense_fifo_info *fifo_info =
		(void *)fifo_info_buffer;

	motion_sense_fifo_get_info(fifo_info, /*reset=*/false);
	TEST_EQ(fifo_info->size, CONFIG_ACCEL_FIFO_SIZE, "%d");

	return EC_SUCCESS;
}

static int test_check_ap_interval_set_one_sample(void)
{
	uint32_t now;
	int read_count;

	/* The AP needs data every sample (200 Hz). */
	motion_sensors[0].config[SENSOR_CONFIG_AP].odr = 200000; /* mHz */
	motion_sensors[0].config[SENSOR_CONFIG_AP].ec_rate = 5000; /* us */
	motion_sensors[0].oversampling_ratio = 1;
	motion_sense_set_data_period(0, 5000 /* us */);
	crec_msleep(5);
	now = __hw_clock_source_read();

	motion_sense_fifo_stage_data(data, motion_sensors, 3, now);
	motion_sense_fifo_commit_data();
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 2, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now, "%u");
	TEST_EQ(motion_sense_fifo_interrupt_needed(), 1, "%d");

	/* Simulate interrupt processing. */
	motion_sense_fifo_reset_needed_flags();

	crec_msleep(5);
	now = __hw_clock_source_read();
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now);
	motion_sense_fifo_commit_data();
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 2, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now, "%u");
	TEST_EQ(motion_sense_fifo_interrupt_needed(), 1, "%d");

	return EC_SUCCESS;
}

static int test_check_ap_interval_set_multiple_sample(void)
{
	uint32_t now;
	int read_count;

	/* The AP needs data every 2 samples. */
	motion_sensors[0].config[SENSOR_CONFIG_AP].odr = 200000; /* mHz */
	motion_sensors[0].config[SENSOR_CONFIG_AP].ec_rate = 10000; /* us */
	motion_sensors[0].oversampling_ratio = 1;
	motion_sense_set_data_period(0, 5000 /* us */);
	crec_msleep(5);
	now = __hw_clock_source_read();

	motion_sense_fifo_stage_data(data, motion_sensors, 3, now);
	motion_sense_fifo_commit_data();
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 2, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now, "%u");
	TEST_EQ(motion_sense_fifo_interrupt_needed(), 0, "%d");

	crec_msleep(5);
	now = __hw_clock_source_read();
	motion_sense_fifo_stage_data(data, motion_sensors, 3, now);
	motion_sense_fifo_commit_data();
	read_count = motion_sense_fifo_read(
		sizeof(data), CONFIG_ACCEL_FIFO_SIZE, data, &data_bytes_read);
	TEST_EQ(read_count, 2, "%d");
	TEST_BITS_SET(data[0].flags, MOTIONSENSE_SENSOR_FLAG_TIMESTAMP);
	TEST_EQ(data[0].timestamp, now, "%u");
	TEST_EQ(motion_sense_fifo_interrupt_needed(), 1, "%d");

	return EC_SUCCESS;
}

void before_test(void)
{
	motion_sense_fifo_commit_data();
	motion_sense_fifo_read(sizeof(data), CONFIG_ACCEL_FIFO_SIZE, &data,
			       &data_bytes_read);
	motion_sense_fifo_reset_needed_flags();
	memset(data, 0, sizeof(data));
	motion_sense_fifo_reset();
}

void run_test(int argc, const char **argv)
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
	RUN_TEST(test_spread_data_on_overflow);
	RUN_TEST(test_spread_data_by_collection_rate);
	RUN_TEST(test_commit_non_data_or_timestamp_entries);
	RUN_TEST(test_get_info_size);
	RUN_TEST(test_check_ap_interval_set_one_sample);
	RUN_TEST(test_check_ap_interval_set_multiple_sample);

	test_print_result();
}
