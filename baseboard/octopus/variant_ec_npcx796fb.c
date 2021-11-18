/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_OCTOPUS_EC_NPCX796FB configuration */

#include "charge_manager.h"
#include "gpio.h"
#include "i2c.h"
#include "power.h"
#ifdef CONFIG_PWM
#include "pwm.h"
#include "pwm_chip.h"
#endif
#include "timer.h"
#include "usbc_ppc.h"
#include "util.h"

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	/* EC_RST_ODL needs to wake device while in PSL hibernate. */
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "battery",
		.port = I2C_PORT_BATTERY,
		.kbps = 100,
		.scl  = GPIO_I2C0_SCL,
		.sda  = GPIO_I2C0_SDA
	},
	{
		.name = "tcpc0",
		.port = I2C_PORT_TCPC0,
		.kbps = 400,
		.scl  = GPIO_I2C1_SCL,
		.sda  = GPIO_I2C1_SDA
	},
	{
		.name = "tcpc1",
		.port = I2C_PORT_TCPC1,
		.kbps = 400,
		.scl  = GPIO_I2C2_SCL,
		.sda  = GPIO_I2C2_SDA
	},
	{
		.name = "eeprom",
		.port = I2C_PORT_EEPROM,
		.kbps = 100,
		.scl  = GPIO_I2C3_SCL,
		.sda  = GPIO_I2C3_SDA
	},
	{
		.name = "charger",
		.port = I2C_PORT_CHARGER,
		.kbps = 100,
		.scl  = GPIO_I2C4_SCL,
		.sda  = GPIO_I2C4_SDA
	},
#ifndef VARIANT_OCTOPUS_NO_SENSORS
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 100,
		.scl  = GPIO_I2C7_SCL,
		.sda  = GPIO_I2C7_SDA
	},
#endif
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#ifdef CONFIG_PWM
/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { .channel = 3, .flags = PWM_CONFIG_DSLEEP,
								.freq = 100 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);
#endif
