/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Careena board-specific configuration */

#include "button.h"
#include "driver/tcpm/ps8xxx.h"
#include "extpower.h"
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
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,   100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1,   400, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"thermal", I2C_PORT_THERMAL_AP, 400, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"sensor",  I2C_PORT_SENSOR,  400, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 5,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * We have total 24 pins for keyboard connecter, {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
		{-1, -1}, {0, 5}, {1, 1}, {1, 0}, {0, 6},
		{0, 7}, {1, 4}, {1, 3}, {1, 6}, {-1, -1},
		{3, 1}, {2, 0}, {1, 5}, {2, 6}, {-1, -1},
		{2, 1}, {2, 4}, {2, 5}, {1, 2}, {2, 3},
		{2, 2}, {3, 0}, {-1, -1}, {-1, -1}, {-1, -1},
};

const int keyboard_factory_scan_pins_used =
			ARRAY_SIZE(keyboard_factory_scan_pins);

static int board_is_support_ps8755_tcpc(void)
{
	/*
	 * 0: PS8751
	 * 1: PS8755
	 */
	return gpio_get_level(GPIO_TCPC_ID);
}

__override uint16_t board_get_ps8xxx_product_id(int port)
{
	/* Careena variant doesn't have ps8xxx product in the port 0 */
	if (port == 0)
		return 0;

	if (board_is_support_ps8755_tcpc())
		return PS8755_PRODUCT_ID;

	return PS8751_PRODUCT_ID;
}
#endif
