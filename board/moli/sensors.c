/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "adc_chip.h"
#include "hooks.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "temp_sensor/thermistor.h"

/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_SSD] = {
		.name = "TEMP_SSD",
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
	[ADC_PPVAR_IMON] = {  /* 20/(20+8.66)*50/200 current divider */
		.name = "PPVAR_IMON",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT * 1433,
		.factor_div = (ADC_READ_MAX + 1) * 250,
	},

};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_SSD] = { .name = "SSD",
				.type = TEMP_SENSOR_TYPE_BOARD,
				.read = get_temp_3v3_30k9_47k_4050b,
				.idx = ADC_TEMP_SENSOR_1_SSD },
	[TEMP_SENSOR_2_CPU_VR] = { .name = "CPU VR",
				   .type = TEMP_SENSOR_TYPE_BOARD,
				   .read = get_temp_3v3_30k9_47k_4050b,
				   .idx = ADC_TEMP_SENSOR_2_CPU_VR },
	[TEMP_SENSOR_4_DIMM] = { .name = "DIMM",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_4_DIMM },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

#define THERMAL_SSD                                         \
	{                                                   \
		.temp_host = {                              \
			[EC_TEMP_THRESH_HALT] = C_TO_K(64), \
		},                                          \
	}
__maybe_unused static const struct ec_thermal_config thermal_ssd = THERMAL_SSD;

#define THERMAL_CPU                                          \
	{                                                    \
		.temp_host = {                               \
			[EC_TEMP_THRESH_HALT] = C_TO_K(100), \
		},                                           \
	}
__maybe_unused static const struct ec_thermal_config thermal_cpu = THERMAL_CPU;

#define THERMAL_DIMM                                        \
	{                                                   \
		.temp_host = {                              \
			[EC_TEMP_THRESH_HALT] = C_TO_K(67), \
		},                                          \
	}
__maybe_unused static const struct ec_thermal_config thermal_dimm =
	THERMAL_DIMM;
/*
 * TODO(b/197478860): add the thermal sensor setting
 */
/* this should really be "const" */
struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_SSD] = THERMAL_SSD,
	[TEMP_SENSOR_2_CPU_VR] = THERMAL_CPU,
	[TEMP_SENSOR_4_DIMM] = THERMAL_DIMM,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
