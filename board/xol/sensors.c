/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "common.h"
#include "driver/als_veml3328.h"
#include "gpio.h"
#include "hooks.h"
#include "motion_sense.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

static struct als_drv_data_t g_veml3328_data = {
	.als_cal.channel_scale = {
		.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kc from VPD */
		.cover_scale = ALS_CHANNEL_SCALE(1.0),     /* CT */
	},
};

/* VEML3328 private data */
static struct veml3328_rgb_drv_data_t g_veml3328_rgb_data = {
	.calib = {
		/* Lux */
		.LG = 1.4143,
		/* xy */
		.A0 = 0.1914,
		.A1 = 0.321,
		.A2 = 0.0,
		.B0 = 0.3339,
		.B1 = 0.0873,
		.B2 = 0.0,
		.Dx_min = 0.27,
		.Dx_max = 0.55,
		.Dy_min = 0.1,
		.Dy_max = 0.65
	},
};

struct motion_sensor_t motion_sensors[] = {
	[CLEAR_ALS] = {
		.name = "Light",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_VEML3328,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &veml3328_drv,
		.drv_data = &g_veml3328_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = VEML3328_I2C_ADDR,
		.min_frequency = VEML3328_MIN_FREQ,
		.max_frequency = VEML3328_MAX_FREQ,
	},
	[RGB_ALS] = {
		.name = "RGB Light",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_VEML3328,
		.type = MOTIONSENSE_TYPE_LIGHT_RGB,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &veml3328_rgb_drv,
		.drv_data = &g_veml3328_rgb_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = VEML3328_I2C_ADDR,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[CLEAR_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

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

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
