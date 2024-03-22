/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc_chip.h"
#include "common.h"
#include "fw_config.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

#if 1
#define CPRINTS(format, args...) ccprints(format, ##args)
#define CPRINTF(format, args...) ccprintf(format, ##args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_DDR_SOC] = {
		.name = "TEMP_DDR_SOC",
		.input_ch = NPCX_ADC_CH0,
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
	[ADC_TEMP_SENSOR_4_CPUCHOKE] = {
		.name = "CPU_CHOKE",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_KSI_00] = {
		.name = "KSI_00",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_KSI_01] = {
		.name = "KSI_01",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_KSI_02] = {
		.name = "KSI_02",
		.input_ch = NPCX_ADC_CH4,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_KSI_03] = {
		.name = "KSI_03",
		.input_ch = NPCX_ADC_CH5,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_KSI_04] = {
		.name = "KSI_04",
		.input_ch = NPCX_ADC_CH8,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_KSI_05] = {
		.name = "KSI_05",
		.input_ch = NPCX_ADC_CH9,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_KSI_06] = {
		.name = "KSI_06",
		.input_ch = NPCX_ADC_CH10,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_KSI_07] = {
		.name = "KSI_07",
		.input_ch = NPCX_ADC_CH11,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_DDR_SOC] = { .name = "DDR and SOC",
				    .type = TEMP_SENSOR_TYPE_BOARD,
				    .read = get_temp_3v3_30k9_47k_4050b,
				    .idx = ADC_TEMP_SENSOR_1_DDR_SOC },
	[TEMP_SENSOR_3_CHARGER] = { .name = "CHARGER",
				    .type = TEMP_SENSOR_TYPE_BOARD,
				    .read = get_temp_3v3_30k9_47k_4050b,
				    .idx = ADC_TEMP_SENSOR_3_CHARGER },
	[TEMP_SENSOR_4_CPUCHOKE] = { .name = "CPU CHOKE",
				     .type = TEMP_SENSOR_TYPE_BOARD,
				     .read = get_temp_3v3_30k9_47k_4050b,
				     .idx = ADC_TEMP_SENSOR_4_CPUCHOKE },
};

BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/201021109): update for Alder Lake/brya
 *
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (100 C)
 */
static const struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(100),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
	},
	.temp_fan_off = C_TO_K(35),
	.temp_fan_max = C_TO_K(70),
};

/*
 * TODO(b/201021109): update for Alder Lake/brya
 *
 * Inductor limits - used for both charger and PP3300 regulator
 *
 * Need to use the lower of the charger IC, PP3300 regulator, and the inductors
 *
 * Charger max recommended temperature 100C, max absolute temperature 125C
 * PP3300 regulator: operating range -40 C to 145 C
 *
 * Inductors: limit of 125c
 * PCB: limit is 100c
 */
static const struct ec_thermal_config thermal_fan = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(100),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
	},
	.temp_fan_off = C_TO_K(35),
	.temp_fan_max = C_TO_K(70),
};

/* this should really be "const" */
struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_DDR_SOC] = thermal_cpu,
	[TEMP_SENSOR_3_CHARGER] = thermal_fan,
	[TEMP_SENSOR_4_CPUCHOKE] = thermal_fan,
};

BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
