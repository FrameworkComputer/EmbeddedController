/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "timer.h"
#include "usb_i2c.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

int usb_i2c_board_is_enabled(void)
{
	return !gpio_get_level(GPIO_EN_PP3300_INA_L);
}

static void ina_disconnect(void)
{
	CPRINTS("Disabling I2C");

	/* Disonnect I2C0 SDA/SCL output to B1/B0 pads */
	GWRITE(PINMUX, DIOB1_SEL, 0);
	GWRITE(PINMUX, DIOB0_SEL, 0);
	/* Disconnect B1/B0 pads to I2C0 input SDA/SCL */
	GWRITE(PINMUX, I2C0_SDA_SEL, 0);
	GWRITE(PINMUX, I2C0_SCL_SEL, 0);

	/* Disable power to INA chips */
	gpio_set_level(GPIO_EN_PP3300_INA_L, 1);
}

static void ina_connect(void)
{
	CPRINTS("Enabling I2C");

	/* Apply power to INA chips */
	gpio_set_level(GPIO_EN_PP3300_INA_L, 0);

	/*
	 * Connect B0/B1 pads to I2C0 input SDA/SCL. Note, that the inputs
	 * for these pads are already enabled for the gpio signals I2C_SCL_INA
	 * and I2C_SDA_INA in gpio.inc.
	 */
	GWRITE(PINMUX, I2C0_SDA_SEL, GC_PINMUX_DIOB1_SEL);
	GWRITE(PINMUX, I2C0_SCL_SEL, GC_PINMUX_DIOB0_SEL);

	/* Connect I2CS SDA/SCL output to B1/B0 pads */
	GWRITE(PINMUX, DIOB1_SEL, GC_PINMUX_I2C0_SDA_SEL);
	GWRITE(PINMUX, DIOB0_SEL, GC_PINMUX_I2C0_SCL_SEL);

	/*
	 * Initialize the i2cm module after the INAs are powered and the signal
	 * lines are connected.
	 */
	i2cm_init();
}

void usb_i2c_board_disable(void)
{
	if (!usb_i2c_board_is_enabled())
		return;

	ina_disconnect();
}

int usb_i2c_board_enable(void)
{
	if (servo_is_connected()) {
		CPRINTS("Servo attached; cannot enable I2C");
		usb_i2c_board_disable();
		return EC_ERROR_BUSY;
	}

	if (!ccd_ext_is_enabled())
		return EC_ERROR_BUSY;

	if (!ccd_is_cap_enabled(CCD_CAP_I2C))
		return EC_ERROR_ACCESS_DENIED;

	if (!usb_i2c_board_is_enabled())
		ina_connect();

	return EC_SUCCESS;
}

/**
 * CCD config change hook
 */
static void ccd_change_i2c(void)
{
	/*
	 * If the capability state doesn't match the current I2C enable state,
	 * try to make them match.
	 */
	if (usb_i2c_board_is_enabled() && !ccd_is_cap_enabled(CCD_CAP_I2C)) {
		/* I2C bridge is enabled, but it's no longer allowed to be */
		usb_i2c_board_disable();
	} else if (!usb_i2c_board_is_enabled() &&
		   ccd_is_cap_enabled(CCD_CAP_I2C)) {
		/*
		 * I2C bridge is disabled, but is allowed to be enabled.  Try
		 * enabling it.  Note that this could fail for several reasons,
		 * such as CCD not connected, or servo attached.  That's ok;
		 * those things will also attempt usb_i2c_board_enable() if
		 * their state changes later.
		 */
		usb_i2c_board_enable();
	}
}

DECLARE_HOOK(HOOK_CCD_CHANGE, ccd_change_i2c, HOOK_PRIO_DEFAULT);
