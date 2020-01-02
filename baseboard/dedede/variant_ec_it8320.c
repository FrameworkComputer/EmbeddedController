/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_DEDEDE_IT8320 configuration */

#include "adc_chip.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "power.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_VSNS_PP3300_A] = {
		"PP3300_A_PGOOD", CHIP_ADC_CH0, ADC_MAX_MVOLT, ADC_READ_MAX+1,
		0},
	[ADC_TEMP_SENSOR_1] = {
		"TEMP_SENSOR1", CHIP_ADC_CH2, ADC_MAX_MVOLT, ADC_READ_MAX+1, 0},

	[ADC_TEMP_SENSOR_2] = {
		"TEMP_SENSOR2", CHIP_ADC_CH3, ADC_MAX_MVOLT, ADC_READ_MAX+1, 0},

	[ADC_SUB_ANALOG] = {
		"SUB_ANALOG", CHIP_ADC_CH13, ADC_MAX_MVOLT, ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* TODO(b/149094481): Set up ADC comparator interrupts for ITE */

/* I2C Ports */
const struct i2c_port_t i2c_ports[] = {
	{
		"eeprom", I2C_PORT_EEPROM, 1000, GPIO_EC_I2C_EEPROM_SCL,
		GPIO_EC_I2C_EEPROM_SDA
	},

	{
		"battery", I2C_PORT_BATTERY, 100, GPIO_EC_I2C_BATTERY_SCL,
		GPIO_EC_I2C_BATTERY_SDA
	},

	{
		"sensor", I2C_PORT_SENSOR, 400, GPIO_EC_I2C_SENSOR_SCL,
		GPIO_EC_I2C_SENSOR_SDA
	},

	{
		"sub_usbc1", I2C_PORT_SUB_USB_C1, 1000,
		GPIO_EC_I2C_SUB_USB_C1_SCL, GPIO_EC_I2C_SUB_USB_C1_SDA
	},

	{
		"usbc0", I2C_PORT_USB_C0, 1000, GPIO_EC_I2C_USB_C0_SCL,
		GPIO_EC_I2C_USB_C0_SDA
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
