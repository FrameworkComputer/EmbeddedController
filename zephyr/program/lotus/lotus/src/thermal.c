/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "gpu.h"
#include "hooks.h"
#include "system.h"
#include "thermal.h"
#include "util.h"

void fan_set_rpm_target(int ch, int rpm)
{
	int board_rpm_max = fans[ch].rpm->rpm_max;
	int board_rpm_min = fans[ch].rpm->rpm_min;

	if (rpm == 0) {
		/* If rpm = 0, disable PWM immediately. Why?*/
		fan_set_duty(ch, 0);
	} else {
		/* This is the counterpart of disabling PWM above. */
		if (!fan_get_enabled(ch)) {
			fan_set_enabled(ch, 1);
		}

		/* Switch the fan configuration when gpu is present */
		if (gpu_present())
			board_rpm_max += (ch == 0) ? 800 : 700;
		if (rpm > board_rpm_max) {
			rpm = board_rpm_max;
		} else if (rpm < board_rpm_min) {
			rpm = board_rpm_min;
		}
	}

	/* Set target rpm */
	fan_data[ch].rpm_target = rpm;
}

int fan_percent_to_rpm(int fan_index, int temp_ratio)
{
	int rpm;
	int max = fans[fan_index].rpm->rpm_max;
	int min = fans[fan_index].rpm->rpm_min;

	if (temp_ratio <= 0) {
		rpm = 0;
	} else {
		/* Switch the fan configuration when gpu is present */
		if (gpu_present())
			max += (fan_index == 0) ? 800 : 700;

		rpm = ((temp_ratio - 1) * max + (100 - temp_ratio) * min) / 99;
	}

	return rpm;
}
