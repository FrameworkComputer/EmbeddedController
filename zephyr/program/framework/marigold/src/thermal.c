/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "amd_stt.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "system.h"
#include "thermal.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"
#include "temperature_filter.h"

#include "temp_sensor/f75303.h"
#include "temp_sensor/f75397.h"

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

uint16_t board_fan_max[2];
uint16_t board_fan_min[2];

#define FAN_STOP_DELAY_S (5 * SECOND)


/*coeff in the form b0, b1, b2, A0, a1, a2 note: drop a0*/
/* 1590,-3130,1590,-31527,15193,14 */
/* */
const int32_t apu_coeff[6] = {
	34, 68, 34, 16384, -30587, 14340
};
struct biquad apu_filtered = {
	.coeff = apu_coeff,
};

static void board_temperature_reset(void)
{
	thermal_filter_reset(&apu_filtered);
}

DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_temperature_reset, HOOK_PRIO_DEFAULT);

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

bool log_thermal;
#define TEMP_BATTERY TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_battery))
// TODO: Rename to SOC
#define TEMP_APU TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_soc))

void board_override_fan_control(int fan, int *temp)
{
	int actual_rpm, new_rpm;
	int apu_temp_mk = 0;

	int apu_pct = 0;
	int pct = 0;
#ifdef CONFIG_GPU
	int apu_selected_pct = 0;
	int current_max_temp_idx = TEMP_APU;
	int apu_filtered_temp = 0;
#endif
	int apu_filtered_pct = 0;
	int temps_mk[5] = {0};

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

		board_get_soc_temp_mk(&apu_temp_mk);
		if (thermal_params[TEMP_APU].temp_fan_off &&
			thermal_params[TEMP_APU].temp_fan_max) {
			apu_pct = thermal_fan_percent(thermal_params[TEMP_APU].temp_fan_off * 1000,
						thermal_params[TEMP_APU].temp_fan_max * 1000,
						apu_temp_mk);
		}

		if (fan == 0) {
			// thermal_filter_update(&apu_filtered, temp[TEMP_APU_DIE]);
		}

		apu_filtered_temp = thermal_filter_get(&apu_filtered);
		if (thermal_params[TEMP_APU_DIE].temp_fan_off &&
			thermal_params[TEMP_APU_DIE].temp_fan_max) {
			apu_filtered_pct = thermal_fan_percent(thermal_params[TEMP_APU_DIE].temp_fan_off*1000,
						thermal_params[TEMP_APU_DIE].temp_fan_max*1000,
						C_TO_K(apu_filtered_temp)*1000);
		}

		apu_selected_pct = MAX(apu_pct, apu_filtered_pct);

		pct = apu_selected_pct;
#else
		board_get_soc_temp_mk(&apu_temp_mk);

		if (thermal_params[TEMP_APU].temp_fan_off &&
			thermal_params[TEMP_APU].temp_fan_max) {
			pct = thermal_fan_percent(thermal_params[TEMP_APU].temp_fan_off * 1000,
						thermal_params[TEMP_APU].temp_fan_max * 1000,
						apu_temp_mk);
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
			if (thermal_params[TEMP_APU].temp_fan_off &&
				apu_temp_mk > (thermal_params[TEMP_APU].temp_fan_off
					* 1000 - 500)) {
				deadline.val = get_time().val + FAN_STOP_DELAY_S;
			}
			if (!timestamp_expired(deadline, &now))
				return;
		} else {
			deadline.val = get_time().val + FAN_STOP_DELAY_S;
		}

		if (log_thermal && fan == 0) {
			f75303_get_val_mk(
				F75303_SENSOR_ID(DT_NODELABEL(ddr_f75303)),
				&temps_mk[0]);
			f75303_get_val_mk(
				F75303_SENSOR_ID(DT_NODELABEL(cpu_f75303)),
				&temps_mk[1]);
			f75397_get_val_mk(
				F75397_SENSOR_ID(DT_NODELABEL(local_f75397)),
				 &temps_mk[2]);
			CPRINTS(
				"\tThrm\t%d\t%d\t%d\t"
				"\t%d\t%d\t"
				"\t%d\t"
				"\t%d\t%d\t%d\t"
				"\t%d\t%d",
				temps_mk[0], temps_mk[1], temps_mk[2],
				temp[TEMP_BATTERY], temp[TEMP_APU],
				thermal_filter_get(&apu_filtered),
				pct, apu_pct, apu_filtered_pct,
				new_rpm, actual_rpm);
		}
		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(FAN_CH(fan), new_rpm);
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/* Stop fan when enter S0ix */
		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(fan, 0);
	}
}



/* EC console command */
static int thermallog_cmd(int argc, const char **argv)
{
	if (argc >= 2) {
		if (!strncmp(argv[1], "en", 2)) {
			log_thermal = true;
			CPRINTS(
				"\tThrm\tDDR\tCPU\tLocal\t"
				"\tBAT\tSOC\t"
				"\tS_f\t"
				"\tPCT\tSpct\tSfilt\t"
				"\tRPM\tFAN"
				);
		} else if (!strncmp(argv[1], "dis", 3)) {
			log_thermal = false;
		} else {
			return EC_ERROR_PARAM1;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermallog, thermallog_cmd, "[en/dis]",
			"Enable or disable thermal logging");
