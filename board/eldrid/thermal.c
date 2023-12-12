/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)

/******************************************************************************/
/* EC thermal management configuration */
/*
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (85 C).
 * TODO(b/170143672): Have different sensor placement. The temperature need to
 * be changed.
 */
const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
};

/*
 * Inductor limits - used for both charger and PP3300 regulator
 *
 * Need to use the lower of the charger IC, PP3300 regulator, and the inductors
 *
 * Charger max recommended temperature 100C, max absolute temperature 125C
 * PP3300 regulator: operating range -40 C to 145 C
 *
 * Inductors: limit of 125c
 * PCB: limit is 80c
 */
const static struct ec_thermal_config thermal_inductor = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_CHARGER] = thermal_inductor,
	[TEMP_SENSOR_2_PP3300_REGULATOR] = thermal_inductor,
	[TEMP_SENSOR_3_DDR_SOC] = thermal_cpu,
	[TEMP_SENSOR_4_FAN] = thermal_cpu,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/******************************************************************************/
struct fan_step {
	/*
	 * Sensor 1~4 trigger point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int8_t on[TEMP_SENSOR_COUNT];

	/*
	 * Sensor 1~4 trigger point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int8_t off[TEMP_SENSOR_COUNT];

	/* Fan rpm */
	uint16_t rpm[FAN_CH_COUNT];
};

/*
 * TODO(b/167931578) Only monitor sensor3 for now.
 * Will add more sensors support if needed.
 */
static const struct fan_step fan_table[] = {
	{
		/* level 0 */
		.on = { -1, -1, 44, -1 },
		.off = { -1, -1, 0, -1 },
		.rpm = { 0 },
	},
	{
		/* level 1 */
		.on = { -1, -1, 46, -1 },
		.off = { -1, -1, 44, -1 },
		.rpm = { 3200 },
	},
	{
		/* level 2 */
		.on = { -1, -1, 50, -1 },
		.off = { -1, -1, 45, -1 },
		.rpm = { 3600 },
	},
	{
		/* level 3 */
		.on = { -1, -1, 54, -1 },
		.off = { -1, -1, 49, -1 },
		.rpm = { 4100 },
	},
	{
		/* level 4 */
		.on = { -1, -1, 58, -1 },
		.off = { -1, -1, 53, -1 },
		.rpm = { 4900 },
	},
	{
		/* level 5 */
		.on = { -1, -1, 60, -1 },
		.off = { -1, -1, 57, -1 },
		.rpm = { 5200 },
	},
};

int fan_table_to_rpm(int fan, int *temp)
{
	/* current fan level */
	static int current_level;
	/* previous fan level */
	static int prev_current_level;

	/* previous sensor temperature */
	static int prev_temp[TEMP_SENSOR_COUNT];
	const int num_fan_levels = ARRAY_SIZE(fan_table);
	int i;
	int new_rpm = 0;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */

	if (temp[TEMP_SENSOR_3_DDR_SOC] < prev_temp[TEMP_SENSOR_3_DDR_SOC]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_SENSOR_3_DDR_SOC] <
			    fan_table[i].off[TEMP_SENSOR_3_DDR_SOC])
				current_level = i - 1;
			else
				break;
		}
	} else if (temp[TEMP_SENSOR_3_DDR_SOC] >
		   prev_temp[TEMP_SENSOR_3_DDR_SOC]) {
		for (i = current_level; i < num_fan_levels; i++) {
			if (temp[TEMP_SENSOR_3_DDR_SOC] >
			    fan_table[i].on[TEMP_SENSOR_3_DDR_SOC])
				current_level = i;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	if (current_level != prev_current_level) {
		CPRINTS("temp: %d, prev_temp: %d", temp[TEMP_SENSOR_3_DDR_SOC],
			prev_temp[TEMP_SENSOR_3_DDR_SOC]);
		CPRINTS("current_level: %d", current_level);
	}

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_temp[i] = temp[i];

	prev_current_level = current_level;

	switch (fan) {
	case FAN_CH_0:
		new_rpm = fan_table[current_level].rpm[FAN_CH_0];
		break;
	default:
		break;
	}

	return new_rpm;
}

void board_override_fan_control(int fan, int *temp)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		fan_set_rpm_mode(FAN_CH(fan), 1);
		fan_set_rpm_target(FAN_CH(fan), fan_table_to_rpm(fan, temp));
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/* Stop fan when enter S0ix */
		fan_set_rpm_mode(FAN_CH(fan), 1);
		fan_set_rpm_target(FAN_CH(fan), 0);
	}
}
