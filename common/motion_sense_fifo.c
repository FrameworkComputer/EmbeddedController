/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "console.h"
#include "hwtimer.h"
#include "mkbp_event.h"
#include "motion_sense_fifo.h"
#include "tablet_mode.h"
#include "task.h"
#include "util.h"
#include "math_util.h"
#include "online_calibration.h"
#include "stdbool.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)

/**
 * Staged metadata for the fifo queue.
 * @read_ts: The timestamp at which the staged data was read. This value will
 *	serve as the upper bound for spreading
 * @count: The total number of motion_sense_fifo entries that are currently
 *	staged.
 * @sample_count: The total number of sensor readings per sensor that are
 *	currently staged.
 * @requires_spreading: Flag used to shortcut the commit process. This should be
 *	true iff at least one of sample_count[] > 1
 */
struct fifo_staged {
	uint32_t read_ts;
	uint16_t count;
	uint8_t sample_count[MAX_MOTION_SENSORS];
	uint8_t requires_spreading;
};

/**
 * Timestamp state metadata for maintaining spreading between commits.
 * @prev: The previous timestamp that was added to the FIFO
 * @next: The predicted next timestamp that will be added to the FIFO
 */
struct timestamp_state {
	uint32_t prev;
	uint32_t next;
};

/** Queue to hold the data to be sent to the AP. */
static struct queue fifo = QUEUE_NULL(CONFIG_ACCEL_FIFO_SIZE,
				      struct ec_response_motion_sensor_data);
/** Count of the number of entries lost due to a small queue. */
static int fifo_lost;
/** Metadata for the fifo, used for staging and spreading data. */
static struct fifo_staged fifo_staged;

/**
 * Cached expected timestamp per sensor. If a sensor's timestamp pre-dates this
 * timestamp it will be fast forwarded.
 */
static struct timestamp_state next_timestamp[MAX_MOTION_SENSORS];

/**
 * Bitmap telling which sensors have valid entries in the next_timestamp array.
 */
static uint32_t next_timestamp_initialized;

/** Need to wake up the AP. */
static int wake_up_needed;

/**
 * Check whether or not a give sensor data entry is a timestamp or not.
 *
 * @param data The data entry to check.
 * @return 1 if the entry is a timestamp, 0 otherwise.
 */
static inline int is_timestamp(
	const struct ec_response_motion_sensor_data *data)
{
	return data->flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP;
}

/**
 * Check whether or not a given sensor data entry contains sensor data or not.
 *
 * @param data The data entry to check.
 * @return True if the entry contains data, false otherwise.
 */
static inline bool is_data(const struct ec_response_motion_sensor_data *data)
{
	return (data->flags & (MOTIONSENSE_SENSOR_FLAG_TIMESTAMP |
			       MOTIONSENSE_SENSOR_FLAG_ODR)) == 0;
}

/**
 * Convenience function to get the head of the fifo. This function makes no
 * guarantee on whether or not the entry is valid.
 *
 * @return Pointer to the head of the fifo.
 */
static inline struct ec_response_motion_sensor_data *get_fifo_head(void)
{
	return ((struct ec_response_motion_sensor_data *) fifo.buffer) +
		(fifo.state->head & fifo.buffer_units_mask);
}

/**
 * Pop one entry from the motion sense fifo. Poping will give priority to
 * committed data (data residing between the head and tail of the queue). If no
 * committed data is available (all the data is staged), then this function will
 * remove the oldest staged data by moving both the head and tail.
 *
 * As a side-effect of this function, it'll updated any appropriate lost and
 * count variables.
 *
 * WARNING: This function MUST be called from within a locked context of
 * g_sensor_mutex.
 */
static void fifo_pop(void)
{
	struct ec_response_motion_sensor_data *head = get_fifo_head();
	const size_t initial_count = queue_count(&fifo);

	/* Check that we have something to pop. */
	if (!initial_count && !fifo_staged.count)
		return;

	/*
	 * If all the data is staged (nothing in the committed queue), we'll
	 * need to move the head and the tail over to simulate poping from the
	 * staged data.
	 */
	if (!initial_count)
		queue_advance_tail(&fifo, 1);

	/*
	 * If we're about to pop a wakeup flag, we should remember it as though
	 * it was committed.
	 */
	if (head->flags & MOTIONSENSE_SENSOR_FLAG_WAKEUP)
		wake_up_needed = 1;
	/*
	 * By not using queue_remove_unit we're avoiding an un-necessary memcpy.
	 */
	queue_advance_head(&fifo, 1);
	fifo_lost++;

	/* Increment lost counter if we have valid data. */
	if (!is_timestamp(head))
		motion_sensors[head->sensor_num].lost++;

	/*
	 * We're done if the initial count was non-zero and we only advanced the
	 * head. Else, decrement the staged count and update staged metadata.
	 */
	if (initial_count)
		return;

	fifo_staged.count--;

	/* If we removed a timestamp there's nothing else for us to do. */
	if (is_timestamp(head))
		return;

	/*
	 * Decrement sample count, if the count was 2 before, we might not need
	 * to spread anymore. Loop through and check.
	 */
	if (--fifo_staged.sample_count[head->sensor_num] < 2) {
		int i;

		fifo_staged.requires_spreading = 0;
		for (i = 0; i < MAX_MOTION_SENSORS; i++) {
			if (fifo_staged.sample_count[i] > 1) {
				fifo_staged.requires_spreading = 1;
				break;
			}
		}
	}
}

/**
 * Make sure that the fifo has at least 1 empty spot to stage data into.
 */
static void fifo_ensure_space(void)
{
	/* If we already have space just bail. */
	if (queue_space(&fifo) > fifo_staged.count)
		return;

	/*
	 * Pop at least 1 spot, but if all the following conditions are met we
	 * will continue to pop:
	 * 1. We're operating with tight timestamps.
	 * 2. The new head isn't a timestamp.
	 * 3. We have data that we can possibly pop.
	 *
	 * Removing more than one entry is needed because if we are using tight
	 * timestamps and we pop a timestamp, then the next head is data, the AP
	 * would assign a bad timestamp to it.
	 */
	do {
		fifo_pop();
	} while (IS_ENABLED(CONFIG_SENSOR_TIGHT_TIMESTAMPS) &&
		 !is_timestamp(get_fifo_head()) &&
		 queue_count(&fifo) + fifo_staged.count);
}

/**
 * Test if a given timestamp is the first timestamp seen by a given sensor
 * number.
 *
 * @param sensor_num the sensor index to test.
 * @return True if the given sensor index has not seen a timestamp yet.
 */
static inline bool is_new_timestamp(uint8_t sensor_num)
{
	return sensor_num < MAX_MOTION_SENSORS &&
	       !(next_timestamp_initialized & BIT(sensor_num));
}

/**
 * Stage a single data unit to the motion sense fifo. Note that for the AP to
 * see this data, it must be committed.
 *
 * @param data The data to stage.
 * @param sensor The sensor that generated the data
 * @param valid_data The number of readable data entries in the data.
 */
static void fifo_stage_unit(
	struct ec_response_motion_sensor_data *data,
	struct motion_sensor_t *sensor,
	int valid_data)
{
	struct queue_chunk chunk;
	int i;

	mutex_lock(&g_sensor_mutex);

	for (i = 0; i < valid_data; i++)
		sensor->xyz[i] = data->data[i];

	/*
	 * For timestamps, update the next value of the sensor's timestamp
	 * if this timestamp is considered new.
	 */
	if (data->flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP &&
	    is_new_timestamp(data->sensor_num)) {
		next_timestamp[data->sensor_num].next =
			next_timestamp[data->sensor_num].prev = data->timestamp;
		next_timestamp_initialized |= BIT(data->sensor_num);
	}

	/* For valid sensors, check if AP really needs this data */
	if (valid_data) {
		int removed = 0;

		if (sensor->oversampling_ratio == 0) {
			removed = 1;
		} else {
			removed = sensor->oversampling++;
			sensor->oversampling %= sensor->oversampling_ratio;
		}
		if (removed) {
			mutex_unlock(&g_sensor_mutex);
			if (IS_ENABLED(CONFIG_ONLINE_CALIB) &&
			    next_timestamp_initialized & BIT(data->sensor_num))
				online_calibration_process_data(
					data, sensor,
					next_timestamp[data->sensor_num].next);
			return;
		}
	}

	/* Make sure we have room for the data */
	fifo_ensure_space();

	if (IS_ENABLED(CONFIG_TABLET_MODE))
		data->flags |= (tablet_get_mode() ?
				MOTIONSENSE_SENSOR_FLAG_TABLET_MODE : 0);

	/*
	 * Get the next writable block in the fifo. We don't need to lock this
	 * because it will always be past the tail and thus the AP will never
	 * read this until motion_sense_fifo_commit_data() is called.
	 */
	chunk = queue_get_write_chunk(&fifo, fifo_staged.count);

	if (!chunk.buffer) {
		/*
		 * This should never happen since we already ensured there was
		 * space, but if there was a bug, we don't want to write to
		 * address 0. Just don't add any data to the queue instead.
		 */
		CPRINTS("Failed to get write chunk for new fifo data!");
		mutex_unlock(&g_sensor_mutex);
		return;
	}

	/*
	 * Save the data to the writable block and increment count. This data
	 * will now reside AFTER the tail of the queue and will not be visible
	 * to the AP until the motion_sense_fifo_commit_data() function is
	 * called. Because count is incremented, the following staged data will
	 * be written to the next available block and this one will remain
	 * staged.
	 */
	memcpy(chunk.buffer, data, fifo.unit_bytes);
	fifo_staged.count++;

	/*
	 * If we're using tight timestamps, and the current entry isn't a
	 * timestamp we'll increment the sample_count for the given sensor.
	 * If the new per-sensor sample count is greater than 1, we'll need to
	 * spread.
	 */
	if (IS_ENABLED(CONFIG_SENSOR_TIGHT_TIMESTAMPS) &&
	    !is_timestamp(data) &&
	    ++fifo_staged.sample_count[data->sensor_num] > 1)
		fifo_staged.requires_spreading = 1;

	mutex_unlock(&g_sensor_mutex);
}

/**
 * Stage an entry representing a single timestamp.
 *
 * @param timestamp The timestamp to add to the fifo.
 * @param sensor_num The sensor number that this timestamp came from (use 0xff
 *	  for unknown).
 */
static void fifo_stage_timestamp(uint32_t timestamp, uint8_t sensor_num)
{
	struct ec_response_motion_sensor_data vector;

	vector.flags = MOTIONSENSE_SENSOR_FLAG_TIMESTAMP;
	vector.timestamp = timestamp;
	vector.sensor_num = sensor_num;
	fifo_stage_unit(&vector, NULL, 0);
}

/**
 * Peek into the staged data at a given offset. This function performs no bound
 * checking and is purely for confinience.
 *
 * @param offset The offset into the staged data to peek into.
 * @return Pointer to the entry at the given offset.
 */
static inline struct ec_response_motion_sensor_data *
peek_fifo_staged(size_t offset)
{
	return (struct ec_response_motion_sensor_data *)
		queue_get_write_chunk(&fifo, offset).buffer;
}

void motion_sense_fifo_init(void)
{
	if (IS_ENABLED(CONFIG_ONLINE_CALIB))
		online_calibration_init();
}

int motion_sense_fifo_wake_up_needed(void)
{
	int res;

	mutex_lock(&g_sensor_mutex);
	res = wake_up_needed;
	mutex_unlock(&g_sensor_mutex);
	return res;
}

void motion_sense_fifo_reset_wake_up_needed(void)
{
	mutex_lock(&g_sensor_mutex);
	wake_up_needed = 0;
	mutex_unlock(&g_sensor_mutex);
}

void motion_sense_fifo_insert_async_event(
	struct motion_sensor_t *sensor,
	enum motion_sense_async_event event)
{
	struct ec_response_motion_sensor_data vector;

	vector.flags = event;
	vector.timestamp = __hw_clock_source_read();
	vector.sensor_num = sensor - motion_sensors;

	fifo_stage_unit(&vector, sensor, 0);
	motion_sense_fifo_commit_data();
}

inline void motion_sense_fifo_add_timestamp(uint32_t timestamp)
{
	fifo_stage_timestamp(timestamp, 0xff);
	motion_sense_fifo_commit_data();
}

void motion_sense_fifo_stage_data(
	struct ec_response_motion_sensor_data *data,
	struct motion_sensor_t *sensor,
	int valid_data,
	uint32_t time)
{
	if (IS_ENABLED(CONFIG_SENSOR_TIGHT_TIMESTAMPS)) {
		/* First entry, save the time for spreading later. */
		if (!fifo_staged.count)
			fifo_staged.read_ts = __hw_clock_source_read();
		fifo_stage_timestamp(time, data->sensor_num);
	}
	fifo_stage_unit(data, sensor, valid_data);
}

void motion_sense_fifo_commit_data(void)
{
	/* Cached data periods, static to store off stack. */
	static uint32_t data_periods[MAX_MOTION_SENSORS];
	struct ec_response_motion_sensor_data *data;
	int i, window, sensor_num;

	/* Nothing staged, no work to do. */
	if (!fifo_staged.count)
		return;

	mutex_lock(&g_sensor_mutex);
	/*
	 * If per-sensor event counts are never more than 1, no spreading is
	 * needed. This will also catch cases where tight timestamps aren't
	 * used.
	 */
	if (!fifo_staged.requires_spreading)
		goto commit_data_end;

	data = peek_fifo_staged(0);

	/*
	 * Spreading only makes sense if tight timestamps are used. In such case
	 * entries are expected to be ordered: timestamp then data. If the first
	 * entry isn't a timestamp we must have gotten out of sync. Just commit
	 * all the data and skip the spreading.
	 */
	if (!is_timestamp(data)) {
		CPRINTS("Spreading skipped, first entry is not a timestamp");
		fifo_staged.requires_spreading = 0;
		goto commit_data_end;
	}

	window = time_until(data->timestamp, fifo_staged.read_ts);

	/* Update the data_periods as needed for this flush. */
	for (i = 0; i < MAX_MOTION_SENSORS; i++) {
		int period;

		/* Skip empty sensors. */
		if (!fifo_staged.sample_count[i])
			continue;

		period = motion_sensors[i].collection_rate;
		/*
		 * Clamp the sample period to the MIN of collection_rate and the
		 * window length / (sample count - 1).
		 */
		if (window && fifo_staged.sample_count[i] > 1)
			period = MIN(
				period,
				window / (fifo_staged.sample_count[i] - 1));
		data_periods[i] = period;
	}

commit_data_end:
	/*
	 * Conditionally spread the timestamps.
	 *
	 * If we got this far that means that the tight timestamps config is
	 * enabled. This means that we can expect the staged entries to have 1
	 * or more timestamps followed by exactly 1 data entry. We'll loop
	 * through the timestamps until we get to data. We only need to update
	 * the timestamp right before it to keep things correct.
	 */
	for (i = 0; i < fifo_staged.count; i++) {
		data = peek_fifo_staged(i);
		if (data->flags & MOTIONSENSE_SENSOR_FLAG_WAKEUP)
			wake_up_needed = 1;

		/*
		 * Skip non-data entries, we don't know the sensor number yet.
		 */
		if (!is_data(data))
			continue;

		/* Get the sensor number and point to the timestamp entry. */
		sensor_num = data->sensor_num;
		data = peek_fifo_staged(i - 1);

		/* Verify we're pointing at a timestamp. */
		if (!is_timestamp(data)) {
			CPRINTS("FIFO entries out of order,"
				" expected timestamp");
			continue;
		}

		/*
		 * If this is the first time we're seeing a timestamp for this
		 * sensor or the timestamp is after our computed next, skip
		 * ahead.
		 */
		if (!(next_timestamp_initialized & BIT(sensor_num)) ||
		    time_after(data->timestamp,
			       next_timestamp[sensor_num].prev)) {
			next_timestamp[sensor_num].next = data->timestamp;
			next_timestamp_initialized |= BIT(sensor_num);
		}

		/* Spread the timestamp and compute the expected next. */
		data->timestamp = next_timestamp[sensor_num].next;
		next_timestamp[sensor_num].prev =
			next_timestamp[sensor_num].next;
		next_timestamp[sensor_num].next +=
			fifo_staged.requires_spreading
			? data_periods[sensor_num]
			: motion_sensors[sensor_num].collection_rate;

		/* Update online calibration if enabled. */
		data = peek_fifo_staged(i);
		if (IS_ENABLED(CONFIG_ONLINE_CALIB))
			online_calibration_process_data(
				data, &motion_sensors[sensor_num],
				next_timestamp[sensor_num].prev);
	}

	/* Advance the tail and clear the staged metadata. */
	queue_advance_tail(&fifo, fifo_staged.count);

	/* Reset metadata for next staging cycle. */
	memset(&fifo_staged, 0, sizeof(fifo_staged));

	mutex_unlock(&g_sensor_mutex);
}

void motion_sense_fifo_get_info(
	struct ec_response_motion_sense_fifo_info *fifo_info,
	int reset)
{
	mutex_lock(&g_sensor_mutex);
	fifo_info->size = fifo.buffer_units;
	fifo_info->count = queue_count(&fifo);
	fifo_info->total_lost = fifo_lost;
	mutex_unlock(&g_sensor_mutex);
#ifdef CONFIG_MKBP_EVENT
	fifo_info->timestamp = mkbp_last_event_time;
#endif

	if (reset)
		fifo_lost = 0;
}

static int motion_sense_get_next_event(uint8_t *out)
{
	union ec_response_get_next_data *data =
		(union ec_response_get_next_data *)out;
	/* out is not padded. It has one byte for the event type */
	motion_sense_fifo_get_info(&data->sensor_fifo.info, 0);
	return sizeof(data->sensor_fifo);
}

DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_SENSOR_FIFO, motion_sense_get_next_event);

inline int motion_sense_fifo_over_thres(void)
{
	int result;

	mutex_lock(&g_sensor_mutex);
	result = queue_space(&fifo) < CONFIG_ACCEL_FIFO_THRES;
	mutex_unlock(&g_sensor_mutex);

	return result;
}

int motion_sense_fifo_read(int capacity_bytes, int max_count, void *out,
			   uint16_t *out_size)
{
	int count;

	mutex_lock(&g_sensor_mutex);
	count = MIN(capacity_bytes / fifo.unit_bytes,
		    MIN(queue_count(&fifo), max_count));
	count = queue_remove_units(&fifo, out, count);
	mutex_unlock(&g_sensor_mutex);
	*out_size = count * fifo.unit_bytes;

	return count;
}

void motion_sense_fifo_reset(void)
{
	next_timestamp_initialized = 0;
	memset(&fifo_staged, 0, sizeof(fifo_staged));
	motion_sense_fifo_init();
	queue_init(&fifo);
}
