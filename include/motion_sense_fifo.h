/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MOTION_SENSE_FIFO_H
#define __CROS_EC_MOTION_SENSE_FIFO_H

#include "motion_sense.h"
#include "task.h"

extern struct queue motion_sense_fifo;
extern int wake_up_needed;
extern int fifo_int_enabled;
extern int fifo_queue_count;
extern int motion_sense_fifo_lost;

enum motion_sense_async_event {
	ASYNC_EVENT_FLUSH = MOTIONSENSE_SENSOR_FLAG_FLUSH |
			    MOTIONSENSE_SENSOR_FLAG_TIMESTAMP,
	ASYNC_EVENT_ODR =   MOTIONSENSE_SENSOR_FLAG_ODR |
			    MOTIONSENSE_SENSOR_FLAG_TIMESTAMP,
};

/**
 * Stage data to the fifo, including a timestamp. This data will not be
 * available to the AP until motion_sense_fifo_commit_data is called.
 *
 * @param data data to insert in the FIFO
 * @param sensor sensor the data comes from
 * @param valid_data data should be copied into the public sensor vector
 * @param time accurate time (ideally measured in an interrupt) the sample
 *             was taken at
 */
void motion_sense_fifo_stage_data(struct ec_response_motion_sensor_data *data,
				  struct motion_sensor_t *sensor,
				  int valid_data,
				  uint32_t time);

/**
 * Commits all staged data to the fifo. If multiple readings were placed using
 * the same timestamps, they will be spread out.
 */
void motion_sense_fifo_commit_data(void);

/**
 * Insert an async event into the fifo.
 *
 * @param sensor Pointer to the sensor generating the event.
 * @param evt The event to insert.
 */
void motion_sense_insert_async_event(struct motion_sensor_t *sensor,
				     enum motion_sense_async_event evt);

/**
 * Stage a timestamp into the fifo.
 *
 * @param timestamp The timestamp to stage.
 */
void motion_sense_fifo_stage_timestamp(uint32_t timestamp);

/**
 * Get information about the fifo.
 *
 * @param fifo_info The struct to store the info.
 */
void motion_sense_get_fifo_info(
	struct ec_response_motion_sense_fifo_info *fifo_info);

/**
 * Checks if either the AP should be woken up due to the fifo.
 *
 * @return 1 if the AP should be woken up, 0 otherwise.
 */
int motion_sense_fifo_is_wake_up_needed(void);

#endif /* __CROS_EC_MOTION_SENSE_FIFO_H */
