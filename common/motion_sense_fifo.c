/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "hwtimer.h"
#include "mkbp_event.h"
#include "motion_sense_fifo.h"
#include "queue.h"
#include "tablet_mode.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)

static inline int is_timestamp(struct ec_response_motion_sensor_data *data)
{
	return data->flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP;
}

/* Need to wake up the AP */
int wake_up_needed;

/* Number of element the AP should collect */
int fifo_queue_count;
int fifo_int_enabled;

struct queue motion_sense_fifo = QUEUE_NULL(
	CONFIG_ACCEL_FIFO_SIZE, struct ec_response_motion_sensor_data);
int motion_sense_fifo_lost;

/**
 * Staged metadata for the motion_sense_fifo.
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
	uint8_t sample_count[SENSOR_COUNT];
	uint8_t requires_spreading;
};
static struct fifo_staged fifo_staged;

static inline struct ec_response_motion_sensor_data *
get_motion_sense_fifo_head(void)
{
	return ((struct ec_response_motion_sensor_data *)
		motion_sense_fifo.buffer) +
	       (motion_sense_fifo.state->head &
		motion_sense_fifo.unit_bytes);
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
static void motion_sense_fifo_pop(void)
{
	struct ec_response_motion_sensor_data *head =
		get_motion_sense_fifo_head();
	const size_t initial_count = queue_count(&motion_sense_fifo);

	/* Check that we have something to pop. */
	if (!initial_count && !fifo_staged.count)
		return;

	/*
	 * If all the data is staged (nothing in the committed queue), we'll
	 * need to move the head and the tail over to simulate poping from the
	 * staged data.
	 */
	if (!initial_count)
		queue_advance_tail(&motion_sense_fifo, 1);

	/*
	 * By not using queue_remove_unit we're avoiding an un-necessary memcpy.
	 */
	queue_advance_head(&motion_sense_fifo, 1);
	motion_sense_fifo_lost++;

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
		for (i = 0; i < SENSOR_COUNT; i++) {
			if (fifo_staged.sample_count[i] > 1) {
				fifo_staged.requires_spreading = 1;
				break;
			}
		}
	}
}

static void motion_sense_fifo_ensure_space(void)
{
	/* If we already have space just bail. */
	if (queue_space(&motion_sense_fifo) > fifo_staged.count)
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
		motion_sense_fifo_pop();
	} while (IS_ENABLED(CONFIG_SENSOR_TIGHT_TIMESTAMPS) &&
		 !is_timestamp(get_motion_sense_fifo_head()) &&
		 queue_count(&motion_sense_fifo) + fifo_staged.count);
}

/*
 * Do not use this function directly if you just want to add sensor data, use
 * motion_sense_fifo_stage_data instead to get a proper timestamp too.
 */
static void motion_sense_fifo_stage_unit(
	struct ec_response_motion_sensor_data *data,
	struct motion_sensor_t *sensor,
	int valid_data)
{
	struct queue_chunk chunk;
	int i;

	mutex_lock(&g_sensor_mutex);

	for (i = 0; i < valid_data; i++)
		sensor->xyz[i] = data->data[i];

	/* For valid sensors, check if AP really needs this data */
	if (valid_data) {
		int removed;

		if (sensor->oversampling_ratio == 0) {
			mutex_unlock(&g_sensor_mutex);
			return;
		}
		removed = sensor->oversampling++;
		sensor->oversampling %= sensor->oversampling_ratio;
		if (removed != 0) {
			mutex_unlock(&g_sensor_mutex);
			return;
		}
	}

	/* Make sure we have room for the data */
	motion_sense_fifo_ensure_space();
	mutex_unlock(&g_sensor_mutex);

	if (data->flags & MOTIONSENSE_SENSOR_FLAG_WAKEUP)
		wake_up_needed = 1;
	if (IS_ENABLED(CONFIG_TABLET_MODE))
		data->flags |= (tablet_get_mode() ?
			MOTIONSENSE_SENSOR_FLAG_TABLET_MODE : 0);

	/*
	 * Get the next writable block in the fifo. We don't need to lock this
	 * because it will always be past the tail and thus the AP will never
	 * read this until motion_sense_fifo_commit_data() is called.
	 */
	chunk = queue_get_write_chunk(
		&motion_sense_fifo, fifo_staged.count);

	if (!chunk.buffer) {
		/*
		 * This should never happen since we already ensured there was
		 * space, but if there was a bug, we don't want to write to
		 * address 0. Just don't add any data to the queue instead.
		 */
		CPRINTS("Failed to get write chunk for new fifo data!");
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
	memcpy(chunk.buffer, data, motion_sense_fifo.unit_bytes);
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
}

void motion_sense_insert_async_event(struct motion_sensor_t *sensor,
				     enum motion_sense_async_event evt)
{
	struct ec_response_motion_sensor_data vector;

	vector.flags = evt;
	vector.timestamp = __hw_clock_source_read();
	vector.sensor_num = sensor - motion_sensors;

	motion_sense_fifo_stage_unit(&vector, sensor, 0);
	motion_sense_fifo_commit_data();
}

void motion_sense_fifo_stage_timestamp(uint32_t timestamp)
{
	struct ec_response_motion_sensor_data vector;

	vector.flags = MOTIONSENSE_SENSOR_FLAG_TIMESTAMP;
	vector.timestamp = timestamp;
	vector.sensor_num = 0;
	motion_sense_fifo_stage_unit(&vector, NULL, 0);
}

void motion_sense_fifo_stage_data(struct ec_response_motion_sensor_data *data,
				  struct motion_sensor_t *sensor,
				  int valid_data,
				  uint32_t time)
{
	if (IS_ENABLED(CONFIG_SENSOR_TIGHT_TIMESTAMPS)) {
		/* First entry, save the time for spreading later. */
		if (!fifo_staged.count)
			fifo_staged.read_ts = __hw_clock_source_read();
		motion_sense_fifo_stage_timestamp(time);
	}
	motion_sense_fifo_stage_unit(data, sensor, valid_data);
}

/**
 * Peek into the staged data at a given offset. This function performs no bound
 * checking and is purely for convenience.
 */
static inline struct ec_response_motion_sensor_data *
motion_sense_peek_fifo_staged(size_t offset)
{
	return (struct ec_response_motion_sensor_data *)
	       queue_get_write_chunk(&motion_sense_fifo, offset).buffer;
}

void motion_sense_fifo_commit_data(void)
{
	/*
	 * Static data to use off stack. Note that next_timestamp should persist
	 * and is only updated if the timestamp from the sensor is greater.
	 */
	static uint32_t data_periods[SENSOR_COUNT];
	static uint32_t next_timestamp[SENSOR_COUNT];
	struct ec_response_motion_sensor_data *data;
	int i, window, sensor_num;

	/* Nothing staged, no work to do. */
	if (!fifo_staged.count)
		return;

	/*
	 * If per-sensor event counts are never more than 1, no spreading is
	 * needed. This will also catch cases where tight timestamps aren't
	 * used.
	 */
	if (!fifo_staged.requires_spreading)
		goto flush_data_end;

	data = motion_sense_peek_fifo_staged(0);

	/*
	 * Spreading only makes sense if tight timestamps are used. In such case
	 * entries are expected to be ordered: timestamp then data. If the first
	 * entry isn't a timestamp we must have gotten out of sync. Just commit
	 * all the data and skip the spreading.
	 */
	if (!is_timestamp(data)) {
		CPRINTS("Spreading skipped, first entry is not a timestamp");
		goto flush_data_end;
	}

	window = time_until(data->timestamp, fifo_staged.read_ts);

	/* Update the data_periods as needed for this flush. */
	for (i = 0; i < SENSOR_COUNT; i++) {
		int period;

		/* Skip empty sensors. */
		if (!fifo_staged.sample_count[i])
			continue;

		period = motion_sensors[i].collection_rate;
		/*
		 * Clamp the sample period to the MIN of collection_rate and the
		 * window length / sample counts.
		 */
		if (window)
			period = MIN(period,
				     window / fifo_staged.sample_count[i]);
		data_periods[i] = period;
	}

	/*
	 * Spread the timestamps.
	 *
	 * If we got this far that means that the tight timestamps config is
	 * enabled. This means that we can expect the staged entries to have 1
	 * or more timestamps followed by exactly 1 data entry. We'll loop
	 * through the timestamps until we get to data. We only need to update
	 * the timestamp right before it to keep things correct.
	 */
	for (i = 0; i < fifo_staged.count; i++) {
		data = motion_sense_peek_fifo_staged(i);

		/* Skip timestamp, we don't know the sensor number yet. */
		if (is_timestamp(data))
			continue;

		/* Get the sensor number and point to the timestamp entry. */
		sensor_num = data->sensor_num;
		data = motion_sense_peek_fifo_staged(i - 1);

		/* If the timestamp is after our computed next, skip ahead. */
		if (time_after(data->timestamp, next_timestamp[sensor_num]))
			next_timestamp[sensor_num] = data->timestamp;

		/* Spread the timestamp and compute the expected next. */
		data->timestamp = next_timestamp[sensor_num];
		next_timestamp[sensor_num] += data_periods[sensor_num];
	}

flush_data_end:
	/* Advance the tail and clear the staged metadata. */
	mutex_lock(&g_sensor_mutex);
	queue_advance_tail(&motion_sense_fifo, fifo_staged.count);
	mutex_unlock(&g_sensor_mutex);

	/* Reset metadata for next staging cycle. */
	memset(&fifo_staged, 0, sizeof(fifo_staged));
}

void motion_sense_get_fifo_info(
	struct ec_response_motion_sense_fifo_info *fifo_info)
{
	fifo_info->size = motion_sense_fifo.buffer_units;
	mutex_lock(&g_sensor_mutex);
	fifo_info->count = fifo_queue_count;
	fifo_info->total_lost = motion_sense_fifo_lost;
	mutex_unlock(&g_sensor_mutex);
	fifo_info->timestamp = mkbp_last_event_time;
}

static int motion_sense_get_next_event(uint8_t *out)
{
	union ec_response_get_next_data *data =
		(union ec_response_get_next_data *)out;
	/* out is not padded. It has one byte for the event type */
	motion_sense_get_fifo_info(&data->sensor_fifo.info);
	return sizeof(data->sensor_fifo);
}

DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_SENSOR_FIFO, motion_sense_get_next_event);

inline int motion_sense_fifo_is_wake_up_needed(void)
{
	return queue_space(&motion_sense_fifo) < CONFIG_ACCEL_FIFO_THRES ||
		wake_up_needed;
}
