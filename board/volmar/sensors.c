/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "adc.h"
#include "hooks.h"
#include "motion_sense.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "temp_sensor/thermistor.h"

/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_DDR_SOC] = {
		.name = "TEMP_DDR_SOC",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_FAN] = {
		.name = "TEMP_FAN",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_3_CHARGER] = {
		.name = "TEMP_CHARGER",
		.input_ch = NPCX_ADC_CH6,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_DDR_SOC] = {
		.name = "DDR and SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_1_DDR_SOC,
	},
	[TEMP_SENSOR_2_FAN] = {
		.name = "FAN",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_2_FAN,
	},
	[TEMP_SENSOR_3_CHARGER] = {
		.name = "CHARGER",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_3_CHARGER,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/180681346): update for Alder Lake/brya
 *
 * Alder Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (85 C)
 */
/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_CPU \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(85), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(65), \
		}, \
		.temp_fan_off = C_TO_K(25), \
		.temp_fan_max = C_TO_K(50), \
	}
__maybe_unused static const struct ec_thermal_config thermal_cpu = THERMAL_CPU;

/*
 * TODO(b/180681346): update for Alder Lake/brya
 *
 * Inductor limits - used for both charger and PP3300 regulator
 *
 * Need to use the lower of the charger IC, PP3300 regulator, and the inductors
 *
 * Charger max recommended temperature 100C, max absolute temperature 125C
 * PP3300 regulator: operating range -40 C to 145 C
 *
 * Inductors: limit of 125c
 * PCB: limit is 85c
 */
/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_FAN \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(85), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(65), \
		}, \
		.temp_fan_off = C_TO_K(25), \
		.temp_fan_max = C_TO_K(50), \
	}
__maybe_unused static const struct ec_thermal_config thermal_fan = THERMAL_FAN;

/*
 * Inductor limits - used for both charger and PP3300 regulator
 *
 * Need to use the lower of the charger IC, PP3300 regulator, and the inductors
 *
 * Charger max recommended temperature 125C, max absolute temperature 150C
 * PP3300 regulator: operating range -40 C to 125 C
 *
 * Inductors: limit of 125c
 * PCB: limit is 85c
 */
/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_CHARGER \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(85), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(65), \
		}, \
		.temp_fan_off = C_TO_K(25), \
		.temp_fan_max = C_TO_K(50), \
	}
__maybe_unused static const struct ec_thermal_config thermal_charger =
							THERMAL_CHARGER;

/* this should really be "const" */
struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_DDR_SOC] = THERMAL_CPU,
	[TEMP_SENSOR_2_FAN] = THERMAL_FAN,
	[TEMP_SENSOR_3_CHARGER]	= THERMAL_CHARGER,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
