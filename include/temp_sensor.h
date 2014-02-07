/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TEMP_SENSOR_H
#define __CROS_EC_TEMP_SENSOR_H

#include "common.h"

/* "enum temp_sensor_id" must be defined for each board in board.h. */
enum temp_sensor_id;

/* Type of temperature sensors. */
enum temp_sensor_type {
	/* Ignore this temperature sensor. */
	TEMP_SENSOR_TYPE_IGNORED = -1,
	/* CPU temperature sensors. */
	TEMP_SENSOR_TYPE_CPU = 0,
	/* Other on-board temperature sensors. */
	TEMP_SENSOR_TYPE_BOARD,
	/* Case temperature sensors. */
	TEMP_SENSOR_TYPE_CASE,
	/* Battery temperature sensors. */
	TEMP_SENSOR_TYPE_BATTERY,

	TEMP_SENSOR_TYPE_COUNT
};

struct temp_sensor_t {
	const char *name;
	/* Temperature sensor type. */
	enum temp_sensor_type type;
	/* Read sensor value in K into temp_ptr; return non-zero if error. */
	int (*read)(int idx, int *temp_ptr);
	/* Index among the same kind of sensors. */
	int idx;
	/* Delay between reading temperature and taking action about it,
	 * in seconds. */
	int action_delay_sec;
};

#ifdef CONFIG_TEMP_SENSOR
/*
 * Defined in board_temp_sensor.c. Must be in the same order as
 * in enum temp_sensor_id.
 */
extern const struct temp_sensor_t temp_sensors[];
#endif

/**
 * Get the most recently measured temperature (in degrees K) for the sensor.
 *
 * @param id		Sensor ID
 * @param temp_ptr	Destination for temperature
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int temp_sensor_read(enum temp_sensor_id id, int *temp_ptr);

#endif  /* __CROS_EC_TEMP_SENSOR_H */
