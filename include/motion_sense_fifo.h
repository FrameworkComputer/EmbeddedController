/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MOTION_SENSE_FIFO_H
#define __CROS_EC_MOTION_SENSE_FIFO_H

#include "motion_sense.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Allowed async events. */
enum motion_sense_async_event {
	ASYNC_EVENT_FLUSH = MOTIONSENSE_SENSOR_FLAG_FLUSH |
			    MOTIONSENSE_SENSOR_FLAG_TIMESTAMP,
	ASYNC_EVENT_ODR = MOTIONSENSE_SENSOR_FLAG_ODR |
			  MOTIONSENSE_SENSOR_FLAG_TIMESTAMP,
};

/**
 * Initialize the motion sense fifo. This function should only be called once.
 */
void motion_sense_fifo_init(void);

/**
 * Set the expected period between samples. Must be call under
 * g_mutex_lock each time the sensor ODR changes.
 *
 * @param sensor_num Affected sensor
 * @param data_period expected milliseconds between samples.
 */
void motion_sense_set_data_period(int sensor_num, uint32_t data_period);

/**
 * Whether or not we need to bypass the FIFO to send an important message.
 *
 * @return Non zero when a bypass is needed.
 */
int motion_sense_fifo_bypass_needed(void);

/**
 * Whether or not we need to interrupt the AP
 *
 * Return true when we have not send FIFO event for a long time.
 *
 * @return Non zero when a wake-up is needed.
 */
int motion_sense_fifo_interrupt_needed(void);

/**
 * Whether or not we need to wake up the AP.
 *
 * When the wakeup flag is set, the bypass flag must be set to.
 *
 * @return Non zero when a wake-up is needed.
 */
int motion_sense_fifo_wake_up_needed(void);

/**
 * Resets the flag for wake up and bypass needed.
 */
void motion_sense_fifo_reset_needed_flags(void);

/**
 * Insert an async event into the fifo.
 *
 * @param sensor The sensor that generated the async event.
 * @param event The event to insert.
 */
void motion_sense_fifo_insert_async_event(struct motion_sensor_t *sensor,
					  enum motion_sense_async_event event);

/**
 * Insert a timestamp into the fifo.
 *
 * @param timestamp The timestamp to insert.
 */
void motion_sense_fifo_add_timestamp(uint32_t timestamp);

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
				  int valid_data, uint32_t time);

/**
 * Commit all the currently staged data to the fifo. Doing so makes it readable
 * to the AP.
 */
void motion_sense_fifo_commit_data(void);

/**
 * Get information about the fifo.
 *
 * @param fifo_info The struct to modify with the current information about the
 *	  fifo. WARNING: This must point to a buffer big enough for the struct
 *	  and also sizeof(uint16_t) * MAX_MOTION_SENSORS of extra space.
 * @param reset Whether or not to reset statistics after reading them.
 */
void motion_sense_fifo_get_info(
	struct ec_response_motion_sense_fifo_info *fifo_info, int reset);

/**
 * Check whether or not the fifo has gone over its threshold.
 *
 * @return 1 if yes, 0 for no.
 */
int motion_sense_fifo_over_thres(void);

/**
 * Read available committed entries from the fifo.
 *
 * @param capacity_bytes The number of bytes available to be written to `out`.
 * @param max_count The maximum number of entries to be placed in `out`.
 * @param out The target to copy the data into.
 * @param out_size The number of bytes written to `out`.
 * @return The number of entries written to `out`.
 */
int motion_sense_fifo_read(int capacity_bytes, int max_count, void *out,
			   uint16_t *out_size);

/**
 * Reset the internal data structures of the motion sense fifo.
 */
__test_only void motion_sense_fifo_reset(void);

#ifdef __cplusplus
}
#endif

#endif /*__CROS_EC_MOTION_SENSE_FIFO_H */
