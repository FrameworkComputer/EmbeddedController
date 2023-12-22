/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "amd_stt.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "gpu.h"
#include "hooks.h"
#include "system.h"
#include "thermal.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"
#include "gpu_configuration.h"

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

uint16_t board_fan_max[2];
uint16_t board_fan_min[2];

#define FAN_STOP_DELAY_S (5 * SECOND)

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
		/* If rpm = 0, wait 10s for make sure temp is cold-down */
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

#define TEMP_APU TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_apu))
#define TEMP_GPU TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_gpu))

void board_override_fan_control(int fan, int *temp)
{
	int actual_rpm, new_rpm;
	int temp_mk = 0;
#ifdef CONFIG_GPU
	int gpu_pct = 0;
#endif
	int apu_pct = 0;
	int pct = 0;
	int current_max_temp_idx = TEMP_APU;
	static timestamp_t deadline;
	timestamp_t now = get_time();

	if (!is_thermal_control_enabled(fan))
		return;

	/*
	 * In common/fan.c pwm_fan_stop() will turn off fan
	 * when chipset suspend or shutdown.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON)) {

#ifdef CONFIG_GPU
		board_get_gpu_temp_mk(&temp_mk);
		if (thermal_params[TEMP_GPU].temp_fan_off &&
			thermal_params[TEMP_GPU].temp_fan_max) {
			gpu_pct = thermal_fan_percent(thermal_params[TEMP_GPU].temp_fan_off * 1000,
						thermal_params[TEMP_GPU].temp_fan_max * 1000,
						temp_mk);
		}

		board_get_soc_temp_mk(&temp_mk);
		if (thermal_params[TEMP_APU].temp_fan_off &&
			thermal_params[TEMP_APU].temp_fan_max) {
			apu_pct = thermal_fan_percent(thermal_params[TEMP_APU].temp_fan_off * 1000,
						thermal_params[TEMP_APU].temp_fan_max * 1000,
						temp_mk);
		}

		if (apu_pct > gpu_pct || !gpu_power_enable())
			pct = apu_pct;
		else {
			current_max_temp_idx = TEMP_GPU;
			pct = gpu_pct;
		}
#else
		board_get_soc_temp_mk(&temp_mk);

		if (thermal_params[TEMP_APU].temp_fan_off &&
			thermal_params[TEMP_APU].temp_fan_max) {
			pct = thermal_fan_percent(thermal_params[TEMP_APU].temp_fan_off * 1000,
						thermal_params[TEMP_APU].temp_fan_max * 1000,
						temp_mk);
		}
#endif

		new_rpm = fan_percent_to_rpm(fan, pct);
		actual_rpm = fan_get_rpm_actual(FAN_CH(fan));

		/*
		 * If we want to turn and the fans are currently significantly below
		 * the minimum turning speed, we should turn at least as fast as the
		 * necessary start speed instead.
		 */
		if (new_rpm && actual_rpm < fans[fan].rpm->rpm_min * 9 / 10 &&
			new_rpm < fans[fan].rpm->rpm_start)
			new_rpm = fans[fan].rpm->rpm_start;

		if (!new_rpm) {
			/* add temperature histeresis so the fan does not turn off
			 * unless the system has cooled 0.5C below the fan turn on temperature
			 */
			if (thermal_params[current_max_temp_idx].temp_fan_off &&
				temp_mk > (thermal_params[current_max_temp_idx].temp_fan_off
					* 1000 - 500)) {
				deadline.val = get_time().val + FAN_STOP_DELAY_S;
			}
			if (!timestamp_expired(deadline, &now))
				return;
		}	else
			deadline.val = get_time().val + FAN_STOP_DELAY_S;


		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(FAN_CH(fan), new_rpm);
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/* Stop fan when enter S0ix */
		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(fan, 0);
	}
}
