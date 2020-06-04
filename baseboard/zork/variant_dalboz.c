/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "charger.h"
#include "common.h"
#include "driver/charger/isl9241.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "temp_sensor.h"

const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_CHARGER] = {
		.name = "CHARGER",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = TEMP_SENSOR_CHARGER,
	},
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = TEMP_SENSOR_SOC,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = sb_tsi_get_val,
		.idx = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const struct i2c_port_t i2c_ports[] = {
	{
		.name = "tcpc0",
		.port = I2C_PORT_TCPC0,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_A0_C0_SCL,
		.sda = GPIO_EC_I2C_USB_A0_C0_SDA,
	},
	{
		.name = "tcpc1",
		.port = I2C_PORT_TCPC1,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_A1_C1_SCL,
		.sda = GPIO_EC_I2C_USB_A1_C1_SDA,
	},
	{
		.name = "charger",
		.port = I2C_PORT_CHARGER,
		.kbps = 100,
		.scl = GPIO_EC_I2C_POWER_SCL,
		.sda = GPIO_EC_I2C_POWER_SDA,
	},
	{
		.name = "ap_mux",
		.port = I2C_PORT_USB_AP_MUX,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USBC_AP_MUX_SCL,
		.sda = GPIO_EC_I2C_USBC_AP_MUX_SDA,
	},
	{
		.name = "thermal",
		.port = I2C_PORT_THERMAL_AP,
		.kbps = 400,
		.scl = GPIO_FCH_SIC,
		.sda = GPIO_FCH_SID,
	},
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl = GPIO_EC_I2C_SENSOR_CBI_SCL,
		.sda = GPIO_EC_I2C_SENSOR_CBI_SDA,
	},
	{
		.name = "ap_audio",
		.port = I2C_PORT_AP_AUDIO,
		.kbps = 400,
		.scl = GPIO_I2C_AUDIO_USB_HUB_SCL,
		.sda = GPIO_I2C_AUDIO_USB_HUB_SDA,
	},
	{
		.name = "battery",
		.port = I2C_PORT_BATTERY_V1,
		.kbps = 100,
		.scl = GPIO_EC_I2C_BATT_SCL,
		.sda = GPIO_EC_I2C_BATT_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/*****************************************************************************
 * Charger
 */

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};
const unsigned int chg_cnt = ARRAY_SIZE(chg_chips);

