/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "lid_switch.h"

#define I2C_ADDR_BACKLIGHT ((0x2C << 1) | I2C_FLAG_BIG_ENDIAN)

#define LP8555_REG_COMMAND           0x00
#define  LP8555_REG_COMMAND_ON       0x01
#define LP8555_REG_CONFIG            0x10
#define  LP8555_REG_CONFIG_MODE_MASK 0x03
#define  LP8555_REG_CONFIG_MODE_PWM  0x00
#define LP8555_REG_CURRENT           0x11
#define  LP8555_REG_CURRENT_MAXCURR_5MA   0x00
#define  LP8555_REG_CURRENT_MAXCURR_10MA  0x01
#define  LP8555_REG_CURRENT_MAXCURR_15MA  0x02
#define  LP8555_REG_CURRENT_MAXCURR_20MA  0x03
#define  LP8555_REG_CURRENT_MAXCURR_23MA  0x04
#define  LP8555_REG_CURRENT_MAXCURR_25MA  0x05
#define  LP8555_REG_CURRENT_MAXCURR_30MA  0x06
#define  LP8555_REG_CURRENT_MAXCURR_50MA  0x07

/**
 * Enable PWM mode in backlight controller and turn it on.
 */
static void lp8555_enable_pwm_mode(void)
{
	int reg;

	/* Enable PWM mode. */
	i2c_read8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		  LP8555_REG_CONFIG, &reg);
	reg &= ~LP8555_REG_CONFIG_MODE_MASK;
	reg |= LP8555_REG_CONFIG_MODE_PWM;
	i2c_write8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		   LP8555_REG_CONFIG, reg);

	/* Set max LED current to 23mA. */
	i2c_write8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		  LP8555_REG_CURRENT, LP8555_REG_CURRENT_MAXCURR_23MA);

	/* Power on. */
	i2c_read8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		  LP8555_REG_COMMAND, &reg);
	reg |= LP8555_REG_COMMAND_ON;
	i2c_write8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		   LP8555_REG_COMMAND, reg);
}
DECLARE_DEFERRED(lp8555_enable_pwm_mode);

/**
 * Host command to toggle backlight.
 */
static int switch_command_enable_backlight(struct host_cmd_handler_args *args)
{
	const struct ec_params_switch_enable_backlight *p = args->params;

	gpio_set_level(GPIO_ENABLE_BACKLIGHT, p->enabled);

	if (p->enabled)
		lp8555_enable_pwm_mode();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SWITCH_ENABLE_BKLIGHT,
		     switch_command_enable_backlight,
		     EC_VER_MASK(0));

void backlight_interrupt(enum gpio_signal signal)
{
	/*
	 * PCH indicates it is turning on backlight so we should
	 * attempt to put the backlight controller into PWM mode.
	 */
	hook_call_deferred(lp8555_enable_pwm_mode, 0);
}

/**
 * Update backlight state.
 */
static void update_backlight(void)
{
	/*
	 * Enable backlight if lid is open; this is AND'd with the request from
	 * the AP in hardware.
	 */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, lid_is_open());
	if (lid_is_open())
		hook_call_deferred(lp8555_enable_pwm_mode, 0);
}
DECLARE_HOOK(HOOK_LID_CHANGE, update_backlight, HOOK_PRIO_DEFAULT);

/**
 * Initialize backlight module.
 */
static void backlight_init(void)
{
	gpio_enable_interrupt(GPIO_PCH_BL_EN);
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, lid_is_open());
}
DECLARE_HOOK(HOOK_INIT, backlight_init, HOOK_PRIO_DEFAULT);
