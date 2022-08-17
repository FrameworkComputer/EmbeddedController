/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Physical fans. These are logically separate from pwm_channels. */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fan_chip.h"
#include "fan.h"
#include "hooks.h"
#include "pwm.h"
#include "timer.h"
#include "thermal.h"
#include "util.h"

#define SENSOR_SOC_FAN_OFF 30
#define SENSOR_SOC_FAN_MID 47
#define SENSOR_SOC_FAN_MAX 53
#define SENSOR_DDR_FAN_TURN_OFF 37
#define SENSOR_DDR_FAN_TURN_ON 38
#define RECORD_TIME (2 * MINUTE)

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

static const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

static const struct fan_rpm rpm_table[FAN_RPM_TABLE_COUNT] = {
	[RPM_TABLE_CPU0] = {
		.rpm_min = 2200,
		.rpm_start = 2200,
		.rpm_max = 3700,
	},

	[RPM_TABLE_CPU1] = {
		.rpm_min = 3700,
		.rpm_start = 3700,
		.rpm_max = 4000,
	},

	[RPM_TABLE_DDR] = {
		.rpm_min = 4000,
		.rpm_start = 4000,
		.rpm_max = 4200,
	},

	[RPM_TABLE_CHARGER] = {
		.rpm_min = 4000,
		.rpm_start = 4000,
		.rpm_max = 4200,
	},

	[RPM_TABLE_AMBIENT] = {
		.rpm_min = 4000,
		.rpm_start = 4000,
		.rpm_max = 4200,
	},
};

struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &rpm_table[RPM_TABLE_CPU0],
	},
};

static void fan_get_rpm(int fan)
{
	static timestamp_t deadline;

	/* Record actual RPM every 2 minutes. */
	if (timestamp_expired(deadline, NULL)) {
		ccprints("fan actual rpm: %d", fan_get_rpm_actual(FAN_CH(fan)));
		deadline.val += RECORD_TIME;
	}
}

static void fan_set_percent(int fan, int pct)
{
	int new_rpm;

	new_rpm = fan_percent_to_rpm(fan, pct);
	fan_set_rpm_target(FAN_CH(fan), new_rpm);
	fan_get_rpm(fan);
}

void board_override_fan_control(int fan, int *tmp)
{
	/*
	 * Crota's fan speed is control by four sensors.
	 *
	 * Sensor charger control the speed when system's temperature
	 * is too high.
	 * Other sensors control normal loading's speed.
	 *
	 * When sensor charger is triggered, the fan speed is only
	 * control by sensor charger, avoid heat damage to system.
	 * When other sensors is triggered, the fan is control
	 * by other sensors.
	 *
	 * Sensor SOC has two slopes for fan speed.
	 * Sensor DDR also become a fan on/off switch.
	 */
	static int pct;
	int sensor_soc;
	int sensor_ddr;
	int sensor_charger;
	int sensor_ambient;

	/* Decide sensor SOC temperature using which slope. */
	if (tmp[TEMP_SENSOR_1_SOC] > SENSOR_SOC_FAN_MID) {
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_off =
			C_TO_K(SENSOR_SOC_FAN_MID);
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_max =
			C_TO_K(SENSOR_SOC_FAN_MAX);
	} else {
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_off =
			C_TO_K(SENSOR_SOC_FAN_OFF);
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_max =
			C_TO_K(SENSOR_SOC_FAN_MID);
	}

	sensor_soc = thermal_fan_percent(
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_off,
		thermal_params[TEMP_SENSOR_1_SOC].temp_fan_max,
		C_TO_K(tmp[TEMP_SENSOR_1_SOC]));
	sensor_ddr = thermal_fan_percent(
		thermal_params[TEMP_SENSOR_2_DDR].temp_fan_off,
		thermal_params[TEMP_SENSOR_2_DDR].temp_fan_max,
		C_TO_K(tmp[TEMP_SENSOR_2_DDR]));
	sensor_charger = thermal_fan_percent(
		thermal_params[TEMP_SENSOR_3_CHARGER].temp_fan_off,
		thermal_params[TEMP_SENSOR_3_CHARGER].temp_fan_max,
		C_TO_K(tmp[TEMP_SENSOR_3_CHARGER]));
	sensor_ambient = thermal_fan_percent(
		thermal_params[TEMP_SENSOR_4_AMBIENT].temp_fan_off,
		thermal_params[TEMP_SENSOR_4_AMBIENT].temp_fan_max,
		C_TO_K(tmp[TEMP_SENSOR_4_AMBIENT]));

	/*
	 * Sensor DDR turn on when temperature > 38,
	 * turn off when temperature < 37
	 */
	if ((tmp[TEMP_SENSOR_2_DDR]) < SENSOR_DDR_FAN_TURN_OFF) {
		pct = 0;
	} else if ((tmp[TEMP_SENSOR_2_DDR]) > SENSOR_DDR_FAN_TURN_ON) {
		/*
		 * Decide which sensor was triggered and choose table.
		 * Priority: charger > soc > ddr > ambient
		 */
		if (sensor_charger) {
			fans[fan].rpm = &rpm_table[RPM_TABLE_CHARGER];
			pct = sensor_charger;
		} else if (sensor_soc) {
			if (tmp[TEMP_SENSOR_1_SOC] > SENSOR_SOC_FAN_MID)
				fans[fan].rpm = &rpm_table[RPM_TABLE_CPU1];
			else
				fans[fan].rpm = &rpm_table[RPM_TABLE_CPU0];
			pct = sensor_soc;
		} else if (sensor_ddr) {
			fans[fan].rpm = &rpm_table[RPM_TABLE_DDR];
			pct = sensor_ddr;
		} else {
			fans[fan].rpm = &rpm_table[RPM_TABLE_AMBIENT];
			pct = sensor_ambient;
		}
	}

	/* Transfer percent to rpm. */
	fan_set_percent(fan, pct);
}
