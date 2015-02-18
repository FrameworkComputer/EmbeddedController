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
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

#define I2C_ADDR_BACKLIGHT ((0x2C << 1) | I2C_FLAG_BIG_ENDIAN)
#define I2C_RETRIES 3
#define I2C_RETRY_DELAY (5*MSEC)

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
#define LP8555_REG_STEP           0x15
#define  LP8555_REG_STEP_STEP_0MS           (0 << 0)
#define  LP8555_REG_STEP_STEP_8MS           (1 << 0)
#define  LP8555_REG_STEP_STEP_16MS          (2 << 0)
#define  LP8555_REG_STEP_STEP_24MS          (3 << 0)
#define  LP8555_REG_STEP_STEP_28MS          (4 << 0)
#define  LP8555_REG_STEP_STEP_32MS          (5 << 0)
#define  LP8555_REG_STEP_STEP_100MS         (6 << 0)
#define  LP8555_REG_STEP_STEP_200MS         (7 << 0)
#define  LP8555_REG_STEP_PWM_IN_HYST_NONE   (0 << 3)
#define  LP8555_REG_STEP_PWM_IN_HYST_1LSB   (1 << 3)
#define  LP8555_REG_STEP_PWM_IN_HYST_2LSB   (2 << 3)
#define  LP8555_REG_STEP_PWM_IN_HYST_4LSB   (3 << 3)
#define  LP8555_REG_STEP_PWM_IN_HYST_8LSB   (4 << 3)
#define  LP8555_REG_STEP_PWM_IN_HYST_16LSB  (5 << 3)
#define  LP8555_REG_STEP_PWM_IN_HYST_32LSB  (6 << 3)
#define  LP8555_REG_STEP_PWM_IN_HYST_64LSB  (7 << 3)
#define  LP8555_REG_STEP_SMOOTH_NONE        (0 << 6)
#define  LP8555_REG_STEP_SMOOTH_LIGHT       (1 << 6)
#define  LP8555_REG_STEP_SMOOTH_MEDIUM      (2 << 6)
#define  LP8555_REG_STEP_SMOOTH_HEAVY       (3 << 6)

/* Read from lp8555 with automatic i2c retries */
static int lp8555_read_with_retry(int reg, int *data)
{
	int i, rv;

	for (i = 0; i < I2C_RETRIES; i++) {
		rv = i2c_read8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
			       reg, data);
		if (rv == EC_SUCCESS)
			return EC_SUCCESS;
		usleep(I2C_RETRY_DELAY);
	}

	CPRINTS("Backlight read fail: reg 0x%02x", reg);
	return rv;
}

/* Write to lp8555 with automatic i2c retries */
static int lp8555_write_with_retry(int reg, int data)
{
	int i, rv;

	for (i = 0; i < I2C_RETRIES; i++) {
		rv = i2c_write8(I2C_PORT_BACKLIGHT, I2C_ADDR_BACKLIGHT,
			       reg, data);
		if (rv == EC_SUCCESS)
			return EC_SUCCESS;
		usleep(I2C_RETRY_DELAY);
	}

	CPRINTS("Backlight write fail: reg 0x%02x data %d", reg, data);
	return rv;
}

/**
 * Setup backlight controller and turn it on.
 */
static void lp8555_enable_pwm_mode(void)
{
	int reg;
	int rv;

	/*
	 * If not in S0, then PCH backlight enable will not be on, and if
	 * lid is closed EC backlight enable will not be on. Since these
	 * two signals are AND'ed together, no point in trying to talk to
	 * the lp8555 if either one of them is not true.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON) || !lid_is_open())
		return;

	/* Enable PWM mode. */
	rv = lp8555_read_with_retry(LP8555_REG_CONFIG, &reg);
	if (rv != EC_SUCCESS)
		return;
	reg &= ~LP8555_REG_CONFIG_MODE_MASK;
	reg |= LP8555_REG_CONFIG_MODE_PWM;
	rv = lp8555_write_with_retry(LP8555_REG_CONFIG, reg);
	if (rv != EC_SUCCESS)
		return;

	/* Set max LED current to 23mA. */
	rv = lp8555_write_with_retry(LP8555_REG_CURRENT,
				     LP8555_REG_CURRENT_MAXCURR_23MA);
	if (rv != EC_SUCCESS)
		return;

	/* Set the rate of brightness change. */
	rv = lp8555_write_with_retry(LP8555_REG_STEP,
				     LP8555_REG_STEP_STEP_200MS |
				     LP8555_REG_STEP_PWM_IN_HYST_8LSB |
				     LP8555_REG_STEP_SMOOTH_HEAVY);
	if (rv != EC_SUCCESS)
		return;

	/* Power on. */
	rv = lp8555_read_with_retry(LP8555_REG_COMMAND, &reg);
	if (rv != EC_SUCCESS)
		return;
	reg |= LP8555_REG_COMMAND_ON;
	rv = lp8555_write_with_retry(LP8555_REG_COMMAND, reg);
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
