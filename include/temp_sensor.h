/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TEMP_SENSOR_H
#define __CROS_EC_TEMP_SENSOR_H

#include "common.h"
#include "board.h"

/* "enum temp_sensor_id" must be defined for each board in board.h. */
enum temp_sensor_id;

struct temp_sensor_t {
	const char* name;
	/* Read sensor value and return temperature in K. */
	int (*read)(int idx);
	/* Index among the same kind of sensors. */
	int idx;
};

/* Initializes the module. */
int temp_sensor_init(void);

/* Returns the most recently measured temperature for the sensor in K,
 * or -1 if error. */
int temp_sensor_read(enum temp_sensor_id id);

#endif  /* __CROS_EC_TEMP_SENSOR_H */
