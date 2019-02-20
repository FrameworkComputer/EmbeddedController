/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"
#include "ccd_config.h"
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
	/* board options use the INA pins as GPIOs */
	if (!board_has_ina_support())
		return 0;

	/*
	 * Note that this signal requires an external pullup, because this is
	 * one of the real open drain pins; we cannot pull it up or drive it
	 * high.  On test boards without the pullup, this will mis-detect as
	 * enabled.
	 */
	return !gpio_get_level(GPIO_EN_PP3300_INA_L);
}

static void ina_disconnect(void)
{
	CPRINTS("I2C disconnect");

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
	CPRINTS("I2C connect");

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
	/* board options use the INA pins as GPIOs */
	if (!board_has_ina_support())
		return EC_SUCCESS;

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
