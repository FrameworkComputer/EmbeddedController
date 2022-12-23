/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "body_detection.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

#define TEMP_AMB TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_amb))

/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_DESKTOP_LID_OPEN \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_WARN] = C_TO_K(44), \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(105), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(110), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_WARN] = C_TO_K(40), \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(95), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(100), \
		}, \
	}
__maybe_unused static const struct ec_thermal_config thermal_desktop_lid_open =
	THERMAL_DESKTOP_LID_OPEN;

/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_DESKTOP_LID_CLOSE \
	{                         \
		.temp_host = { \
			[EC_TEMP_THRESH_WARN] = C_TO_K(43), \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(105), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(110), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_WARN] = C_TO_K(39), \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(95), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(100), \
		},  \
	}
__maybe_unused static const struct ec_thermal_config thermal_desktop_lid_close =
	THERMAL_DESKTOP_LID_CLOSE;

/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_LAPTOP           \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_WARN] = C_TO_K(44), \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(105), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(110), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_WARN] = C_TO_K(40), \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(95), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(100), \
		}, \
	}
__maybe_unused static const struct ec_thermal_config thermal_laptop =
	THERMAL_LAPTOP;

static int last_amb_temp = -1;

/* Switch thermal table when mode change */
static void thermal_table_switch(void)
{
	enum body_detect_states body_state = body_detect_get_state();

	if (body_state == BODY_DETECTION_OFF_BODY) {
		if (lid_is_open()) {
			thermal_params[TEMP_AMB] = thermal_desktop_lid_open;
			CPRINTS("Thermal: Desktop lid open mode");
		} else {
			thermal_params[TEMP_AMB] = thermal_desktop_lid_close;
			CPRINTS("Thermal: Desktop lid close mode");
		}
	} else {
		thermal_params[TEMP_AMB] = thermal_laptop;
		CPRINTS("Thermal: Laptop mode");
	}
}
DECLARE_HOOK(HOOK_INIT, thermal_table_switch, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_LID_CHANGE, thermal_table_switch, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BODY_DETECT_CHANGE, thermal_table_switch, HOOK_PRIO_DEFAULT);

/* Set SCI event to host for temperature change */
static void detect_temp_change(void)
{
	int t, rv;

	rv = temp_sensor_read(TEMP_AMB, &t);
	if (rv == EC_SUCCESS) {
		if (last_amb_temp != t) {
			last_amb_temp = t;
			host_set_single_event(EC_HOST_EVENT_THERMAL_THRESHOLD);
		}
	} else if (rv == EC_ERROR_INVAL) {
		CPRINTS("Temp sensor: Invalid id");
	}
}
DECLARE_HOOK(HOOK_SECOND, detect_temp_change, HOOK_PRIO_TEMP_SENSOR_DONE);
