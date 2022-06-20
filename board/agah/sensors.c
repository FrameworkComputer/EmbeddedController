/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "adc.h"
#include "hooks.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "temp_sensor/thermistor.h"

/* ADC configuration */
struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_DDR_SOC] = {
		.name = "TEMP_DDR_SOC",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_GPU] = {
		.name = "TEMP_GPU",
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
	[ADC_CHARGER_IADP] = {
		.name = "CHARGER_IADP",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_ADP_TYP] = {
		.name = "ADP_TYP",
		.input_ch = NPCX_ADC_CH4,
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
	[TEMP_SENSOR_2_GPU] = {
		.name = "GPU",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_2_GPU,
	},
	[TEMP_SENSOR_3_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_3_CHARGER,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

#define THERMAL_CPU \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(85), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(90), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80), \
		}, \
		.temp_fan_off = C_TO_K(35), \
		.temp_fan_max = C_TO_K(60), \
	}
__maybe_unused static const struct ec_thermal_config thermal_cpu = THERMAL_CPU;

#define THERMAL_GPU \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(85), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(90), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80), \
		}, \
		.temp_fan_off = C_TO_K(35), \
		.temp_fan_max = C_TO_K(60), \
	}
__maybe_unused static const struct ec_thermal_config thermal_gpu =
	THERMAL_GPU;

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
#define THERMAL_CHARGER \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(105), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(120), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90), \
		}, \
		.temp_fan_off = C_TO_K(35), \
		.temp_fan_max = C_TO_K(65), \
	}
__maybe_unused static const struct ec_thermal_config thermal_charger =
	THERMAL_CHARGER;

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_DDR_SOC] = THERMAL_CPU,
	[TEMP_SENSOR_2_GPU] = THERMAL_GPU,
	[TEMP_SENSOR_3_CHARGER] = THERMAL_CHARGER,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
