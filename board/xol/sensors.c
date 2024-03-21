/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1] = {
		.name = "TEMP_MEMORY",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2] = {
		.name = "TEMP_AMBIENT",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_3] = {
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
	[TEMP_SENSOR_1] = {
		.name = "Memory",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_51k1_47k_4050b,
		.idx = ADC_TEMP_SENSOR_1,
	},
	[TEMP_SENSOR_2] = {
		.name = "Ambient",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_51k1_47k_4050b,
		.idx = ADC_TEMP_SENSOR_2,
	},
	[TEMP_SENSOR_3] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_51k1_47k_4050b,
		.idx = ADC_TEMP_SENSOR_3,
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
static const struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(90),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
	},
	.temp_fan_off = C_TO_K(35),
	.temp_fan_max = C_TO_K(60),
};

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
 * PCB: limit is 80c
 */
static const struct ec_thermal_config thermal_ambient = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(90),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
	},
	.temp_fan_off = C_TO_K(35),
	.temp_fan_max = C_TO_K(60),
};

/*
 * Inductor limits - used for both charger and PP3300 regulator
 *
 * Need to use the lower of the charger IC, PP3300 regulator, and the inductors
 *
 * Charger max recommended temperature 125C, max absolute temperature 150C
 * PP3300 regulator: operating range -40 C to 125 C
 *
 * Inductors: limit of 125c
 * PCB: limit is 80c
 */
const static struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(105),
		[EC_TEMP_THRESH_HALT] = C_TO_K(120),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
	},
	.temp_fan_off = C_TO_K(35),
	.temp_fan_max = C_TO_K(65),
};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1] = thermal_cpu,
	[TEMP_SENSOR_2] = thermal_ambient,
	[TEMP_SENSOR_3] = thermal_charger,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
