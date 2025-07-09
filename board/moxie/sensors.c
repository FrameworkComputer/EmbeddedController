/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc_chip.h"
#include "common.h"
#include "hooks.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_CPU] = {
		.name = "TEMP_CPU",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_CPU_VR] = {
		.name = "TEMP_CPU_VR",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_3_WIFI] = {
		.name = "TEMP_WIFI",
		.input_ch = NPCX_ADC_CH6,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_4_DIMM] = {
		.name = "TEMP_DIMM",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_VBUS] = {  /* 5/39 voltage divider */
		.name = "VBUS",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT * 39,
		.factor_div = (ADC_READ_MAX + 1) * 5,
	},
	[ADC_PPVAR_IMON] = {  /* 20/(20+8.66)*50/200 */
		.name = "PPVAR_IMON",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT * 143,
		.factor_div = (ADC_READ_MAX + 1) * 25,
	},

};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_CPU] = { .name = "CPU",
				.type = TEMP_SENSOR_TYPE_BOARD,
				.read = get_temp_3v3_30k9_47k_4050b,
				.idx = ADC_TEMP_SENSOR_1_CPU },
	[TEMP_SENSOR_2_CPU_VR] = { .name = "CPU VR",
				   .type = TEMP_SENSOR_TYPE_BOARD,
				   .read = get_temp_3v3_30k9_47k_4050b,
				   .idx = ADC_TEMP_SENSOR_2_CPU_VR },
	[TEMP_SENSOR_3_WIFI] = { .name = "WIFI",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_3_WIFI },
	[TEMP_SENSOR_4_DIMM] = { .name = "DIMM",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_4_DIMM },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/238260272): add the thermal sensor setting
 */
#define THERMAL_CPU              \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(90), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HALT] = C_TO_K(70), \
		}, \
		.temp_fan_off = C_TO_K(35), \
		.temp_fan_max = C_TO_K(89), \
	}
__maybe_unused static const struct ec_thermal_config thermal_cpu = THERMAL_CPU;

#define THERMAL_FAN              \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = 0, \
			[EC_TEMP_THRESH_HALT] = 0, \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = 0, \
		}, \
		.temp_fan_off = 0, \
		.temp_fan_max = 0, \
	}
__maybe_unused static const struct ec_thermal_config thermal_fan = THERMAL_FAN;

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_CPU] = THERMAL_CPU,
	[TEMP_SENSOR_2_CPU_VR] = THERMAL_FAN,
	[TEMP_SENSOR_3_WIFI] = THERMAL_FAN,
	[TEMP_SENSOR_4_DIMM] = THERMAL_FAN,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
