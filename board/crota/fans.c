/* Copyright 2022 The ChromiumOS Authors
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
#include "tablet_mode.h"
#include "timer.h"
#include "thermal.h"
#include "util.h"

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
	[RPM_TABLE_CPU] = {
		.rpm_min = 0,
		.rpm_start = 0,
		.rpm_max = 4000,
	},

	[RPM_TABLE_CPU_TABLET] = {
		.rpm_min = 0,
		.rpm_start = 0,
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
		.rpm = &rpm_table[RPM_TABLE_CPU],
	},
};

static const struct thermal_policy_config
	thermal_cfg[THERMAL_CFG_TABLE_COUNT] = {
	[LAPTOP_MODE] = {
		.fan_off_slop1 = 24,
		.fan_max_slop1 = 51,
		.fan_off_slop2 = 29,
		.fan_max_slop2 = 48,
		.fan_slop_threshold = 45,
		.ddr_fan_turn_off = 38,
		.ddr_fan_turn_on = 44,
		.rpm_table_cpu = RPM_TABLE_CPU,
	},

	[TABLET_MODE] = {
		.fan_off_slop1 = 25,
		.fan_max_slop1 = 52,
		.fan_off_slop2 = 30,
		.fan_max_slop2 = 49,
		.fan_slop_threshold = 45,
		.ddr_fan_turn_off = 38,
		.ddr_fan_turn_on = 44,
		.rpm_table_cpu = RPM_TABLE_CPU_TABLET,
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
	const struct thermal_policy_config *t;
	static int pct;
	int i;
	int fan_pct[TEMP_SENSOR_COUNT];
	int fan_off;
	int fan_max;

	/* Decide is tablet mode or laptop mode. */
	if (tablet_get_mode())
		t = &thermal_cfg[TABLET_MODE];
	else
		t = &thermal_cfg[LAPTOP_MODE];

	/* Decide sensor SOC temperature using which slope. */
	if (tmp[TEMP_SENSOR_1_SOC] <= t->fan_slop_threshold) {
		fan_off = t->fan_off_slop1;
		fan_max = t->fan_max_slop1;
	} else {
		fan_off = t->fan_off_slop2;
		fan_max = t->fan_max_slop2;
	}
	thermal_params[TEMP_SENSOR_1_SOC].temp_fan_off = C_TO_K(fan_off);
	thermal_params[TEMP_SENSOR_1_SOC].temp_fan_max = C_TO_K(fan_max);

	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		fan_pct[i] = thermal_fan_percent(thermal_params[i].temp_fan_off,
						 thermal_params[i].temp_fan_max,
						 C_TO_K(tmp[i]));
	}

	/*
	 * In Balance mode:
	 * Sensor DDR turn on when temperature > 44,
	 * turn off when temperature < 38
	 *
	 * In Tablet mode:
	 * Sensor DDR turn on when temperature > 44,
	 * turn off when temperature < 38
	 *
	 * When temperature from high dropping to 38 ~ 44,
	 * if pct is not 0, keep sensor trigger and choose table.
	 */
	if (((tmp[TEMP_SENSOR_2_DDR]) <= t->ddr_fan_turn_on && pct == 0) ||
	    ((tmp[TEMP_SENSOR_2_DDR]) < t->ddr_fan_turn_off))
		pct = 0;
	else {
		/*
		 * Decide which sensor was triggered and choose table.
		 * Priority: charger > soc > ddr > ambient
		 */
		if (fan_pct[TEMP_SENSOR_3_CHARGER]) {
			fans[fan].rpm = &rpm_table[RPM_TABLE_CHARGER];
			pct = fan_pct[TEMP_SENSOR_3_CHARGER];
		} else if (fan_pct[TEMP_SENSOR_1_SOC]) {
			fans[fan].rpm = &rpm_table[t->rpm_table_cpu];
			pct = fan_pct[TEMP_SENSOR_1_SOC];
		} else if (fan_pct[TEMP_SENSOR_2_DDR]) {
			fans[fan].rpm = &rpm_table[RPM_TABLE_DDR];
			pct = fan_pct[TEMP_SENSOR_2_DDR];
		} else {
			fans[fan].rpm = &rpm_table[RPM_TABLE_AMBIENT];
			pct = fan_pct[TEMP_SENSOR_4_AMBIENT];
		}
	}

	/* Transfer percent to rpm. */
	fan_set_percent(fan, pct);
}
