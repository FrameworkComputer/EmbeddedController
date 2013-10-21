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

#define LP8555_REG_COMMAND           0x00
#define  LP8555_REG_COMMAND_ON       0x01
#define LP8555_REG_CONFIG            0x10
#define  LP8555_REG_CONFIG_MODE_MASK 0x03
#define  LP8555_REG_CONFIG_MODE_PWM  0x00

/**
 * Enable PWM mode in backlight controller and turn it on.
 */
static int lp8555_enable_pwm_mode(void)
{
	int reg;

	/* Enable PWM mode. */
	i2c_read8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		  LP8555_REG_CONFIG, &reg);
	reg &= ~LP8555_REG_CONFIG_MODE_MASK;
	reg |= LP8555_REG_CONFIG_MODE_PWM;
	i2c_write8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		   LP8555_REG_CONFIG, reg);

	/* Power on. */
	i2c_read8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		  LP8555_REG_COMMAND, &reg);
	reg |= LP8555_REG_COMMAND_ON;
	i2c_write8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		   LP8555_REG_COMMAND, reg);

	return EC_SUCCESS;
}

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

/**
 * Hook to turn backlight PWM mode on if it turns off.
 */
static void backlight_pwm_mode_hook(void)
{
	int reg;

	/* Only check if the system is powered. */
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return;

	/* Read current command reg to see if it is on. */
	i2c_read8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
		  LP8555_REG_COMMAND, &reg);

	/* Turn it on if needed. */
	if (!(reg & LP8555_REG_COMMAND_ON))
		lp8555_enable_pwm_mode();
}
DECLARE_HOOK(HOOK_SECOND, backlight_pwm_mode_hook, HOOK_PRIO_LAST);

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
}
DECLARE_HOOK(HOOK_LID_CHANGE, update_backlight, HOOK_PRIO_DEFAULT);

/**
 * Initialize backlight module.
 */
static void backlight_init(void)
{
	update_backlight();
}
DECLARE_HOOK(HOOK_INIT, backlight_init, HOOK_PRIO_DEFAULT);
