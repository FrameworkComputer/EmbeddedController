/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Grunt board-specific configuration */

#include "button.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/led/lm3630a.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"

#include "gpio_list.h"

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "power",
		.port = I2C_PORT_POWER,
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
		.name = "thermal",
		.port = I2C_PORT_THERMAL_AP,
		.kbps = 400,
		.scl  = GPIO_I2C3_SCL,
		.sda  = GPIO_I2C3_SDA
	},
	{
		.name = "kblight",
		.port = I2C_PORT_KBLIGHT,
		.kbps = 100,
		.scl  = GPIO_I2C5_SCL,
		.sda  = GPIO_I2C5_SDA
	},
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl  = GPIO_I2C7_SCL,
		.sda  = GPIO_I2C7_SDA
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 5,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
	[PWM_CH_LED1_AMBER] = {
		.channel = 0,
		.flags = (PWM_CONFIG_OPEN_DRAIN | PWM_CONFIG_ACTIVE_LOW
			  | PWM_CONFIG_DSLEEP),
		.freq = 100,
	},
	[PWM_CH_LED2_BLUE] =   {
		.channel = 2,
		.flags = (PWM_CONFIG_OPEN_DRAIN | PWM_CONFIG_ACTIVE_LOW
			  | PWM_CONFIG_DSLEEP),
		.freq = 100,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

void board_update_sensor_config_from_sku(void)
{
	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_6AXIS_INT_L);
}

static void board_kblight_init(void)
{
	/*
	 * Enable keyboard backlight. This needs to be done here because
	 * the chip doesn't have power until PP3300_S0 comes up.
	 */
	gpio_set_level(GPIO_KB_BL_EN, 1);
	lm3630a_poweron();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_kblight_init, HOOK_PRIO_DEFAULT);
