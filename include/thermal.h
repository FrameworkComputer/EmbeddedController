/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermal engine module for Chrome EC */

#ifndef __CROS_EC_THERMAL_H
#define __CROS_EC_THERMAL_H

#include "util.h"

#define THERMAL_CONFIG_NO_FLAG 0x0
#define THERMAL_CONFIG_WARNING_ON_FAIL 0x1

/* Set a threshold temperature to this value to disable the threshold limit. */
#define THERMAL_THRESHOLD_DISABLE 0

/* This macro is used to disable all threshold for a sensor.
 * The value 0 expands to all field in the array 'thresholds'. Change this
 * if THERMAL_THRESHOLD_DISABLE is no longer 0.
 */
#define THERMAL_THRESHOLD_DISABLE_ALL 0

enum thermal_threshold {
	/* Low fan speed */
	THRESHOLD_FAN_LO = 0,
	/* High fan speed */
	THRESHOLD_FAN_HI,
	/* Issue overheating warning */
	THRESHOLD_WARNING,
	/* Shut down CPU */
	THRESHOLD_CPU_DOWN,
	/* Shut down everything we can */
	THRESHOLD_POWER_DOWN,

	THRESHOLD_COUNT
};

/* Configuration for temperature sensor. Temperature value in degree K. */
struct thermal_config_t {
	/* Configuration flags. */
	int8_t config_flags;
	/* Threshold temperatures. */
	int16_t thresholds[THRESHOLD_COUNT];
};

/* Set the threshold temperature value. Return -1 on error. */
int thermal_set_threshold(int sensor_id, int threshold_id, int value);

/* Get the threshold temperature value. Return -1 on error. */
int thermal_get_threshold(int sensor_id, int threshold_id);

/* Toggle automatic fan speed control. Return -1 on error. */
int thermal_toggle_auto_fan_ctrl(int auto_fan_on);

#endif  /* __CROS_EC_THERMAL_H */
