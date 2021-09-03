/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "adc_chip.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "temp_sensor/thermistor.h"

/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_FAN] = {
		.name = "TEMP_FAN",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_SOC] = {
		.name = "TEMP_SOC",
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
	[ADC_TEMP_SENSOR_4_REGULATOR] = {
		.name = "TEMP_REGULATOR",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_FAN] = {
		.name = "Fan",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_1_FAN
	},
	[TEMP_SENSOR_2_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_2_SOC
	},
	[TEMP_SENSOR_3_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_3_CHARGER
	},
	[TEMP_SENSOR_4_REGULATOR] = {
		.name = "Regulator",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_4_REGULATOR
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/199246802): Need to update for Alder Lake/anahera
 */
static const struct ec_thermal_config thermal_fan = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
};

/*
 * TODO(b/199246802): Need to update for Alder Lake/anahera
 *
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to SOC, so we need to use the lower
 * SOC temperature limit (85 C)
 */
static const struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
};

/*
 * TODO(b/199246802): Need to update for Alder Lake/anahera
 */
static const struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		[EC_TEMP_THRESH_HALT] = C_TO_K(85),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
	},
};

/*
 * TODO(b/199246802): Need to update for Alder Lake/anahera
 */
static const struct ec_thermal_config thermal_regulator = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		[EC_TEMP_THRESH_HALT] = C_TO_K(85),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
	},
};

/* this should really be "const" */
struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_FAN] = thermal_fan,
	[TEMP_SENSOR_2_SOC] = thermal_cpu,
	[TEMP_SENSOR_3_CHARGER] = thermal_charger,
	[TEMP_SENSOR_4_REGULATOR] = thermal_regulator,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
