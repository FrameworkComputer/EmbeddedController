/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "util.h"

static int fan_control[FAN_CH_COUNT];

void fan_set_percent(int fan, int pct)
{
	int actual_rpm;
	int new_rpm;
	const int min_rpm = fans[fan].rpm->rpm_min * 9 / 10;

	new_rpm = fan_percent_to_rpm(fan, pct);
	actual_rpm = fan_get_rpm_actual(FAN_CH(fan));

	if (new_rpm &&
	    actual_rpm < min_rpm &&
	    new_rpm < fans[fan].rpm->rpm_start)
		new_rpm = fans[fan].rpm->rpm_start;

	fan_set_rpm_target(FAN_CH(fan), new_rpm);
}

void board_override_fan_control(int fan, int *tmp)
{
	int i, f;
	int fmax = 0;
	int temp_fan_configured = 0;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		tmp[i] = C_TO_K(tmp[i]);

		/* figure out the max fan needed */
		if (thermal_params[i].temp_fan_off &&
		    thermal_params[i].temp_fan_max) {
			f = thermal_fan_percent(thermal_params[i].temp_fan_off,
						thermal_params[i].temp_fan_max,
						tmp[i]);
			if (i == TEMP_GPU)
				fan_control[FAN_CH_1] = f;
			else {
				if (f > fmax) {
					fan_control[FAN_CH_0] = f;
					fmax = f;
				} else
					fan_control[FAN_CH_0] = fmax;
			}
			temp_fan_configured = 1;
		}
	}
	/* transfer percent to rpm */
	if (temp_fan_configured)
		fan_set_percent(fan, fan_control[fan]);
}
