/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermal engine module for Chrome EC */

#ifndef __CROS_EC_THERMAL_H
#define __CROS_EC_THERMAL_H

#include "temp_sensor.h"

#define THERMAL_CONFIG_NO_FLAG 0x0
#define THERMAL_CONFIG_WARNING_ON_FAIL 0x1

/*
 * Number of steps for fan speed control. Speed of each step is defined
 * in thermal.c.
 */
#define THERMAL_FAN_STEPS 5

/* Set a threshold temperature to this value to disable the threshold limit. */
#define THERMAL_THRESHOLD_DISABLE 0

/* This macro is used to disable all threshold for a sensor.  The value 0
 * expands to all field in the array 'thresholds'. Change this if
 * THERMAL_THRESHOLD_DISABLE is no longer 0.
 */
#define THERMAL_THRESHOLD_DISABLE_ALL 0

enum thermal_threshold {
	THRESHOLD_WARNING = 0,	/* Issue overheating warning */
	THRESHOLD_CPU_DOWN,	/* Shut down CPU */
	THRESHOLD_POWER_DOWN,	/* Shut down everything we can */
	THRESHOLD_COUNT
};

/* Configuration for temperature sensor */
struct thermal_config_t {
	/* Configuration flags */
	int8_t config_flags;
	/* Threshold temperatures in K */
	int16_t thresholds[THRESHOLD_COUNT + THERMAL_FAN_STEPS];
};

/**
 * Set a threshold temperature.
 *
 * @param type		Sensor type to set threshold for
 * @param threshold_id	Threshold ID to set
 * @param value		New threshold temperature in K, or
 *			THERMAL_THRESHOLD_DISABLE to disable this threshold.
 *
 * @return EC_SUCCESS if success, non-zero if error.
 */
int thermal_set_threshold(enum temp_sensor_type type, int threshold_id,
			  int value);

/**
 * Read a threshold temperature.
 *
 * @param type		Sensor type to get threshold for
 * @param threshold_id	Threshold ID
 *
 * @return The threshold temperature in K, THERMAL_THRESHOLD_DISABLE if
 * disabled, -1 if error.
 */
int thermal_get_threshold(enum temp_sensor_type type, int threshold_id);

/**
 * Enable/disable automatic fan speed control
 *
 * @param enable	Enable (!=0) or disable (0) auto fan control
 */
void thermal_control_fan(int enable);

#endif  /* __CROS_EC_THERMAL_H */
