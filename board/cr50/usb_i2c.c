/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "device_state.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "timer.h"
#include "usb_i2c.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

static int i2c_enabled(void)
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
DECLARE_DEFERRED(ina_disconnect);

static void ina_connect(void)
{
	CPRINTS("Enabling I2C");

	/* Apply power to INA chips */
	gpio_set_level(GPIO_EN_PP3300_INA_L, 0);
	/* Allow enough time for power rail to come up */
	usleep(25);

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

void usb_i2c_board_disable(int debounce)
{
	if (!i2c_enabled())
		return;

	/*
	 * Wait to disable i2c in case we are doing a bunch of i2c transactions
	 * in a row.
	 */
	hook_call_deferred(&ina_disconnect_data, debounce ? 1 * SECOND : 0);
}

int usb_i2c_board_enable(void)
{
	if (device_get_state(DEVICE_SERVO) != DEVICE_STATE_OFF) {
		CPRINTS("Servo is attached I2C cannot be enabled");
		usb_i2c_board_disable(0);
		return EC_ERROR_BUSY;
	}

	hook_call_deferred(&ina_disconnect_data, -1);

	if (!i2c_enabled())
		ina_connect();
	return EC_SUCCESS;
}
