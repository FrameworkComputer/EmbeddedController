/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_DEDEDE_IT8320 configuration */

#include "adc_chip.h"
#include "atomic.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "power.h"
#include "registers.h"

#define CPRINTUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)

static void pp3300_a_pgood_low(void)
{
	deprecated_atomic_clear_bits(&pp3300_a_pgood, 1);

	/* Disable low interrupt while asserted */
	vcmp_enable(VCMP_SNS_PP3300_LOW, 0);

	/* Enable high interrupt */
	vcmp_enable(VCMP_SNS_PP3300_HIGH, 1);

	/*
	 * Call power_signal_interrupt() with a fake GPIO in order for the
	 * chipset task to pick up the change in power sequencing signals.
	 */
	power_signal_interrupt(GPIO_PG_EC_DSW_PWROK);
}

static void pp3300_a_pgood_high(void)
{
	deprecated_atomic_or(&pp3300_a_pgood, 1);

	/* Disable high interrupt while asserted */
	vcmp_enable(VCMP_SNS_PP3300_HIGH, 0);

	/* Enable low interrupt */
	vcmp_enable(VCMP_SNS_PP3300_LOW, 1);

	/*
	 * Call power_signal_interrupt() with a fake GPIO in order for the
	 * chipset task to pick up the change in power sequencing signals.
	 */
	power_signal_interrupt(GPIO_PG_EC_DSW_PWROK);
}

const struct vcmp_t vcmp_list[] = {
	[VCMP_SNS_PP3300_LOW] = {
		.name = "VCMP_SNS_PP3300_LOW",
		.threshold = 600, /* mV */
		.flag = LESS_EQUAL_THRESHOLD,
		.vcmp_thresh_cb = &pp3300_a_pgood_low,
		.scan_period = VCMP_SCAN_PERIOD_600US,
		.adc_ch = CHIP_ADC_CH0,
	},
	[VCMP_SNS_PP3300_HIGH] = {
		.name = "VCMP_SNS_PP3300_HIGH",
		.threshold = 2700, /* mV */
		.flag = GREATER_THRESHOLD,
		.vcmp_thresh_cb = &pp3300_a_pgood_high,
		.scan_period = VCMP_SCAN_PERIOD_600US,
		.adc_ch = CHIP_ADC_CH0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(vcmp_list) <= CHIP_VCMP_COUNT);
BUILD_ASSERT(ARRAY_SIZE(vcmp_list) == VCMP_COUNT);

/* I2C Ports */
const struct i2c_port_t i2c_ports[] = {
	{
		"eeprom", I2C_PORT_EEPROM, 400, GPIO_EC_I2C_EEPROM_SCL,
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

#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
	{
		"sub_usbc1", I2C_PORT_SUB_USB_C1, 1000,
		GPIO_EC_I2C_SUB_USB_C1_SCL, GPIO_EC_I2C_SUB_USB_C1_SDA
	},
#endif

	{
		"usbc0", I2C_PORT_USB_C0, 1000, GPIO_EC_I2C_USB_C0_SCL,
		GPIO_EC_I2C_USB_C0_SDA
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

