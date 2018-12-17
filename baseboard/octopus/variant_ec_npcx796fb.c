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
	{"battery", I2C_PORT_BATTERY, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1,   400, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"eeprom",  I2C_PORT_EEPROM,  100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"charger", I2C_PORT_CHARGER, 100, GPIO_I2C4_SCL, GPIO_I2C4_SDA},
#ifndef VARIANT_OCTOPUS_NO_SENSORS
	{"sensor",  I2C_PORT_SENSOR,  100, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
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
