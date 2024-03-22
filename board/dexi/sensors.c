/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc_chip.h"
#include "common.h"
#include "hooks.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_VSNS_PP3300_A] = { .name = "PP3300_A_PGOOD",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH0 },
	[ADC_TEMP_SENSOR_1] = { .name = "TEMP_SENSOR1",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH2 },
	[ADC_TEMP_SENSOR_2] = { .name = "TEMP_SENSOR2",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH3 },
	[ADC_VBUS] = { .name = "VBUS", /* 113/1113 voltage divider */
		       .factor_mul = ADC_MAX_MVOLT * 1113,
		       .factor_div = (ADC_READ_MAX + 1) * 113,
		       .shift = 0,
		       .channel = CHIP_ADC_CH4 },
	[ADC_TEMP_SENSOR_3] = { .name = "TEMP_SENSOR3",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH13 },
	/* 0.01 ohm shunt resistor and 50 V/V INA -> 500 mV/A */
	[ADC_PPVAR_PWR_IN_IMON] = { .name = "ADC_PPVAR_PWR_IN_IMON",
				    .factor_mul = ADC_MAX_MVOLT * 2,
				    .factor_div = ADC_READ_MAX + 1,
				    .shift = 0,
				    .channel = CHIP_ADC_CH15 },
	/* 5/39 voltage divider */
	[ADC_SNS_PPVAR_PWR_IN] = { .name = "ADC_SNS_PPVAR_PWR_IN",
				   .factor_mul = ADC_MAX_MVOLT * 39,
				   .factor_div = (ADC_READ_MAX + 1) * 5,
				   .shift = 0,
				   .channel = CHIP_ADC_CH16 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Memory",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "SoC power",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Ambient",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* add the thermal sensor setting */
#define THERMAL_MEMORY           \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(85), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(70), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(80), \
		}, \
		.temp_fan_off = 0, \
		.temp_fan_max = 0, \
	}
__maybe_unused static const struct ec_thermal_config thermal_memory =
	THERMAL_MEMORY;

#define THERMAL_SOC              \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(85), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(70), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(80), \
		}, \
		.temp_fan_off = 0, \
		.temp_fan_max = 0, \
	}
__maybe_unused static const struct ec_thermal_config thermal_soc = THERMAL_SOC;

#define THERMAL_AMBIENT          \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(85), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(70), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(80), \
		}, \
		.temp_fan_off = 0, \
		.temp_fan_max = 0, \
	}
__maybe_unused static const struct ec_thermal_config thermal_ambient =
	THERMAL_AMBIENT;

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1] = THERMAL_MEMORY,
	[TEMP_SENSOR_2] = THERMAL_SOC,
	[TEMP_SENSOR_3] = THERMAL_AMBIENT,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
