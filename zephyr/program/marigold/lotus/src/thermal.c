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
#include "gpu_configuration.h"

uint16_t board_fan_max[2];
uint16_t board_fan_min[2];

void fan_configure_gpu(struct gpu_cfg_fan *fan) {
	if (fan == NULL) {
		board_fan_max[0] = 0;
		board_fan_max[1] = 0;
		board_fan_min[0] = 0;
		board_fan_min[1] = 0;
	} else {
		if (fan->idx < 2) {
			board_fan_max[fan->idx] = fan->max_rpm;
			board_fan_min[fan->idx] = fan->min_rpm;
		}
	}
}
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

		/* Switch the fan configuration when gpu is present else use default */
		if (board_fan_max[ch]) {
			board_rpm_max = board_fan_max[ch];
		}
		if (board_fan_min[ch]) {
			board_rpm_min = board_fan_min[ch];
		}
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
		if (board_fan_max[fan_index]) {
			max = board_fan_max[fan_index];
		}
		if (board_fan_min[fan_index]) {
			min = board_fan_min[fan_index];
		}
		rpm = ((temp_ratio - 1) * max + (100 - temp_ratio) * min) / 99;
	}

	return rpm;
}
