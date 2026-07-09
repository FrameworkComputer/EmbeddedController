/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "amd_stt.h"
#include "board_host_command.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "fan.h"
#include "gpu.h"
#include "hooks.h"
#include "system.h"
#include "thermal.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"
#include "gpu_configuration.h"
#include "temperature_filter.h"

#include "temp_sensor/f75303.h"

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

uint16_t board_fan_max[2];
uint16_t board_fan_min[2];
uint8_t board_fan_flags[2];
uint8_t board_custom_temp_flag;
#define FAN_STOP_DELAY_S (5 * SECOND)

#define TEMP_APU TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_apu))
#define TEMP_GPU TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_gpu))
#define TEMP_GPU_DIE TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_gpu_die))
#define TEMP_APU_DIE TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_apu_die))


#ifdef CONFIG_GPU
_STATIC_ASSERT(TEMP_APU == 2, "TEMP_APU must be 2 to match with GPU configuration");
_STATIC_ASSERT(TEMP_APU_DIE == 3, "TEMP_APU_DIE must be 3 to match with GPU configuration");
_STATIC_ASSERT(TEMP_GPU == 5, "TEMP_GPU must be 5 to match with GPU configuration");
_STATIC_ASSERT(TEMP_GPU_DIE == 7, "TEMP_GPU_DIE must be 7 to match with GPU configuration");
#endif

/*coeff in the form b0, b1, b2, A0, a1, a2 note: drop a0*/
/* 1590,-3130,1590,-31527,15193,14 */
/* */
const int32_t gpu_coeff[6] = {
	59, 119,  59, 16384, -29863, 13716
};
const int32_t apu_coeff[6] = {
	34, 68, 34, 16384, -30587, 14340
};
struct biquad gpu_filtered = {
	.coeff = gpu_coeff,
};
struct biquad apu_filtered = {
	.coeff = apu_coeff,
};


static const uint8_t temp_sensor_id_to_f75303_sensor_id[TEMP_SENSOR_COUNT] = {
	[TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_ambient))] = F75303_SENSOR_ID(DT_NODELABEL(ambient_f75303)),
	[TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_charger))] = F75303_SENSOR_ID(DT_NODELABEL(charger_f75303)),
	[TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_apu))] = F75303_SENSOR_ID(DT_NODELABEL(apu_f75303)),
	[TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_gpu_amb))] = F75303_SENSOR_ID(DT_NODELABEL(gpu_amb_f75303)),
	[TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_gpu))] = F75303_SENSOR_ID(DT_NODELABEL(gpu_vr_f75303)),
	[TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_gpu_vram))] = F75303_SENSOR_ID(DT_NODELABEL(gpu_vram_f75303))
};

static void board_temperature_reset(void)
{
	thermal_filter_reset(&gpu_filtered);
	thermal_filter_reset(&apu_filtered);
}

DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_temperature_reset, HOOK_PRIO_DEFAULT);

void fan_configure_gpu(struct gpu_cfg_fan *fan) {
	if (fan == NULL) {
		board_fan_max[0] = 0;
		board_fan_max[1] = 0;
		board_fan_min[0] = 0;
		board_fan_min[1] = 0;
		board_fan_flags[0] = 0;
		board_fan_flags[1] = 0;
	} else {
		if (fan->idx < 2) {
			board_fan_max[fan->idx] = fan->max_rpm;
			board_fan_min[fan->idx] = fan->min_rpm;
			board_fan_flags[fan->idx] = fan->flags;
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

	/* Switch the fan configuration when gpu is present */
	if (board_fan_max[fan_index]) {
		max = board_fan_max[fan_index];
	}

	if (board_fan_min[fan_index]) {
		min = board_fan_min[fan_index];
	}

	if (temp_ratio <= 0) {
		if (board_fan_flags[fan_index] & GPU_CFG_FAN_FLAG_ALWAYS_ON)
			rpm = min;
		else
			rpm = 0;
	} else {
		rpm = ((temp_ratio - 1) * max + (100 - temp_ratio) * min) / 99;
	}

	return rpm;
}

bool log_thermal;
bool log_thermal_oneshot;
void board_override_fan_control(int fan, int *temp)
{
	int actual_rpm, new_rpm, i;
	int max_idx = -1;
	static int prev_max_idx = -1;
	int max_pct = 0;

	static int temps_mk[TEMP_SENSOR_COUNT];

	static timestamp_t deadline;
	timestamp_t now = get_time();

	if (!is_thermal_control_enabled(fan))
		return;

	/*
	 * In common/fan.c pwm_fan_stop() will turn off fan
	 * when chipset suspend or shutdown.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND)) {


		/* if we have more than one fan only update on the first loop */
		if (fan == 0) {
#ifdef CONFIG_GPU
			thermal_filter_update(&gpu_filtered, temp[TEMP_GPU_DIE]);
#endif /* CONFIG_GPU */
			thermal_filter_update(&apu_filtered, temp[TEMP_APU_DIE]);
		}



		for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
			int temperature_mk = 0;
			int sensor_pct = -1;
			/* Select the sensor source and read the value in millikelvin */
			switch(i) {

				case TEMP_APU_DIE:
					temperature_mk = thermal_filter_get(&apu_filtered);
					temperature_mk = C_TO_K(temperature_mk)*1000;
				break;

#ifdef CONFIG_GPU
				case TEMP_GPU_DIE:
					temperature_mk = thermal_filter_get(&gpu_filtered);
					temperature_mk = C_TO_K(temperature_mk)*1000;
				break;

				case TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_gpu_amb)):
				case TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_gpu)):
				case TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_gpu_vram)):

#endif /* CONFIG_GPU */
				case TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_ambient)):
				case TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_charger)):
				case TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_apu)):
					/* use a lookup to map TEMP_SENSOR_ID to f75303_SENSOR_ID */
					f75303_get_val_mk(
						temp_sensor_id_to_f75303_sensor_id[i],
						&temperature_mk);
				break;

				default:
					/* todo we should get this in millikelvin instead if possible */
					temperature_mk = C_TO_K(temp[i])*1000;
				break;

			}

			temps_mk[i] = temperature_mk;

			if (thermal_params[i].temp_fan_off &&
				thermal_params[i].temp_fan_max) {
				sensor_pct = thermal_fan_percent(thermal_params[i].temp_fan_off * 1000,
							thermal_params[i].temp_fan_max * 1000,
							temperature_mk);

				if ((sensor_pct > max_pct)) {
					if (gpu_present()) {
						/* skip sensors if we are a GPU and GPU is unpowered */
						if ((i == TEMP_GPU_DIE || i == TEMP_GPU) &&
							!gpu_is_working()) {
							continue;
						}
					}
					max_pct = sensor_pct;
					max_idx = i;
				}
			}
		}

		if (max_idx != -1) {
			prev_max_idx = max_idx;
		}

		new_rpm = fan_percent_to_rpm(fan, max_pct);
		actual_rpm = fan_get_rpm_actual(FAN_CH(fan));

		/*
		 * If we want to turn and the fans are currently significantly below
		 * the minimum turning speed, we should turn at least as fast as the
		 * necessary start speed instead.
		 */
		if (new_rpm && actual_rpm < fans[fan].rpm->rpm_min * 9 / 10 &&
			new_rpm < fans[fan].rpm->rpm_start)
			new_rpm = fans[fan].rpm->rpm_start;

		if (!new_rpm && prev_max_idx != -1) {
			/* add temperature hysteresis so the fan does not turn off
			 * unless the system has cooled 0.5C below the fan turn on temperature
			 */
			if (thermal_params[prev_max_idx].temp_fan_off &&
				temps_mk[prev_max_idx] > (thermal_params[prev_max_idx].temp_fan_off
					* 1000 - 500)) {
				deadline.val = get_time().val + FAN_STOP_DELAY_S;
			}

			/* dont change the fan duty during the timeout - exit early */
			if (!timestamp_expired(deadline, &now))
				return;
		} else {
			deadline.val = get_time().val + FAN_STOP_DELAY_S;
		}

		if (log_thermal && fan == 0) {
			ccprintf("\tThrm\t");
			for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
				/* highlight the current max pct sensor temp with a * */
				ccprintf("%s%d\t",max_idx == i ? "*" : " ", K_TO_C(temps_mk[i]/1000));
			}

			ccprintf("%d\t%d\t%d\t%d\n",
				max_pct, max_idx,
				new_rpm, actual_rpm);
		}

		if (log_thermal_oneshot && fan == 0) {
			for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {

				int sensor_pct = -1;

				if (thermal_params[i].temp_fan_off &&
					thermal_params[i].temp_fan_max) {
					sensor_pct = thermal_fan_percent(thermal_params[i].temp_fan_off * 1000,
								thermal_params[i].temp_fan_max * 1000,
								temps_mk[i]);
				}

				ccprintf("  %-20s  ", temp_sensors[i].name);
				ccprintf("%d C (= %d C raw)", K_TO_C(temps_mk[i]/1000), temp[i]);

				if (sensor_pct != -1) {
					ccprintf("  %11d%% (%d C and %d C) %s",
						sensor_pct,
						K_TO_C(thermal_params[i].temp_fan_off),
						K_TO_C(thermal_params[i].temp_fan_max),
						max_idx == i ? "<" : " "
					);
				}
				ccprintf("\n");
			}
			ccprintf("  Fan %d%% by %-20s = (Target RPM=%d, Actual RPM=%d)\n",
				max_pct,
				max_idx == -1 ? "None" : temp_sensors[max_idx].name,
				new_rpm,
				actual_rpm);
			log_thermal_oneshot = false;
		}


		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(FAN_CH(fan), new_rpm);
	}
}



/* EC console command */
static int thermallog_cmd(int argc, const char **argv)
{
	int i;
	if (argc >= 2) {
		if (!strncmp(argv[1], "en", 2)) {
			log_thermal = true;
			ccprintf("\tThrm\t");

			for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
				ccprintf("%-10s\t", temp_sensors[i].name);

			}
			ccprintf("max_pct\tmax_idx\tRPM_tgt\tFAN_actual\n");

		} else if (!strncmp(argv[1], "dis", 3)) {
			log_thermal = false;
		} else if (!strncmp(argv[1], "print", 5))  {
			log_thermal_oneshot = true;
		} else {
			return EC_ERROR_PARAM1;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermallog, thermallog_cmd, "[en/dis/print]",
			"Enable or disable thermal logging");


static const struct ec_thermal_config dsc_amd_temp_gpu_table = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(71),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(97),
		[EC_TEMP_THRESH_HALT] = C_TO_K(105),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(60),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
	},
	.temp_fan_off = C_TO_K(50),
	.temp_fan_max = C_TO_K(82),
};

static const struct ec_thermal_config dsc_amd_temp_gpu_die_table = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(87),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(105),
		[EC_TEMP_THRESH_HALT] = C_TO_K(108),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(60),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(60),
	},
	.temp_fan_off = C_TO_K(50),
	.temp_fan_max = C_TO_K(80),
};

static const struct ec_thermal_config dsc_nv_temp_gpu_table = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(71),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(105),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(60),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(60),
	},
	.temp_fan_off = C_TO_K(50),
	.temp_fan_max = C_TO_K(82),
};

static const struct ec_thermal_config dsc_nv_temp_gpu_die_table = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(87),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(97),
		[EC_TEMP_THRESH_HALT] = C_TO_K(100),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(60),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(60),
	},
	.temp_fan_off = C_TO_K(50),
	.temp_fan_max = C_TO_K(80),
};

void thermal_table_clear_gpu_override(void)
{
	board_custom_temp_flag = 0;
}

void thermal_table_set_gpu_override(uint8_t idx)
{
	board_custom_temp_flag |= (1 << idx);
}

bool thermal_table_override(uint8_t sensor)
{
	return (board_custom_temp_flag & (1 << sensor)) != 0;
}

void thermal_table_switch_by_gpu_type(void)
{
	uint8_t gpu_vendor = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_TYPE);

	switch (gpu_vendor) {
	case GPU_AMD_R23M:
		if (!thermal_table_override(TEMP_GPU))
			thermal_params[TEMP_GPU] = dsc_amd_temp_gpu_table;

		if (!thermal_table_override(TEMP_GPU_DIE))
			thermal_params[TEMP_GPU_DIE] = dsc_amd_temp_gpu_die_table;
		break;

	case GPU_NV_GN22:
		if (!thermal_table_override(TEMP_GPU))
			thermal_params[TEMP_GPU] = dsc_nv_temp_gpu_table;

		if (!thermal_table_override(TEMP_GPU_DIE))
			thermal_params[TEMP_GPU_DIE] = dsc_nv_temp_gpu_die_table;
		break;

	default:
		thermal_params[TEMP_APU].temp_fan_off =
			C_TO_K(DT_PROP_OR(DT_NODELABEL(temp_sensor_apu), temp_fan_off, -273));
		thermal_params[TEMP_APU].temp_fan_max =
			C_TO_K(DT_PROP_OR(DT_NODELABEL(temp_sensor_apu), temp_fan_max, -273));
		break;
	}
}
