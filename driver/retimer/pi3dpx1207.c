/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PI3DPX1207 retimer.
 */

#include "pi3dpx1207.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "ioexpander.h"
#include "usb_mux.h"

#define I2C_MAX_RETRIES 2

/* Stack space is limited, so put the buffer somewhere else */
static uint8_t buf[PI3DPX1207_NUM_REGISTERS];

/**
 * Local utility functions
 */
static int pi3dpx1207_i2c_write(int i2c_port,
				uint16_t addr_flags,
				uint8_t offset,
				uint8_t val)
{
	int rv = EC_SUCCESS;
	int attempt;

	if (offset >= PI3DPX1207_NUM_REGISTERS)
		return EC_ERROR_INVAL;

	/*
	 * PI3DPX1207 does not support device register offset in
	 * the typical I2C sense. Have to read the values starting
	 * from 0, modify the byte and then write the block.
	 *
	 * NOTE: The device may not respond correctly if it was
	 * just powered or has gone to sleep.  Allow for retries
	 * in case this happens.
	 */
	if (offset > 0) {
		attempt = 0;
		do {
			attempt++;
			rv = i2c_xfer(i2c_port, addr_flags,
				      NULL, 0, buf, offset);
		} while ((rv != EC_SUCCESS) && (attempt < I2C_MAX_RETRIES));
	}

	if (rv == EC_SUCCESS) {
		buf[offset] = val;

		attempt = 0;
		do {
			attempt++;
			rv = i2c_xfer(i2c_port, addr_flags,
				      buf, offset + 1, NULL, 0);
		} while ((rv != EC_SUCCESS) && (attempt < I2C_MAX_RETRIES));
	}
	return rv;
}

static void pi3dpx1207_shutoff_power(int port)
{
	const int gpio_enable		= usb_retimers[port].gpio_enable;
	const int gpio_dp_enable	= usb_retimers[port].gpio_dp_enable;

	gpio_or_ioex_set_level(gpio_enable, 0);
	gpio_or_ioex_set_level(gpio_dp_enable, 0);
}

/**
 * Driver interface code
 */
static int pi3dpx1207_init(int port)
{
	const int gpio_enable		= usb_retimers[port].gpio_enable;

	gpio_or_ioex_set_level(gpio_enable, 1);
	return EC_SUCCESS;
}

static int pi3dpx1207_enter_low_power_mode(int port)
{
	pi3dpx1207_shutoff_power(port);
	return EC_SUCCESS;
}

static int pi3dpx1207_set_mux(int port, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	uint8_t mode_val		= PI3DPX1207_MODE_WATCHDOG_EN;

	const int i2c_port		= usb_retimers[port].i2c_port;
	const uint16_t i2c_addr_flags	= usb_retimers[port].i2c_addr_flags;
	const int gpio_enable		= usb_retimers[port].gpio_enable;
	const int gpio_dp_enable	= usb_retimers[port].gpio_dp_enable;

	/* USB */
	if (mux_state & MUX_USB_ENABLED) {
		gpio_or_ioex_set_level(gpio_enable, 1);
		/* USB with DP */
		if (mux_state & MUX_DP_ENABLED) {
			gpio_or_ioex_set_level(gpio_dp_enable, 1);
			mode_val |= (mux_state & MUX_POLARITY_INVERTED)
					? PI3DPX1207_MODE_CONF_USB_DP_FLIP
					: PI3DPX1207_MODE_CONF_USB_DP;
		}
		/* USB without DP */
		else {
			gpio_or_ioex_set_level(gpio_dp_enable, 0);
			mode_val |= (mux_state & MUX_POLARITY_INVERTED)
					? PI3DPX1207_MODE_CONF_USB_FLIP
					: PI3DPX1207_MODE_CONF_USB;
		}
	}
	/* DP without USB */
	else if (mux_state & MUX_DP_ENABLED) {
		gpio_or_ioex_set_level(gpio_enable, 1);
		gpio_or_ioex_set_level(gpio_dp_enable, 1);
		mode_val |= (mux_state & MUX_POLARITY_INVERTED)
				? PI3DPX1207_MODE_CONF_DP_FLIP
				: PI3DPX1207_MODE_CONF_DP;
	}
	/* Nothing enabled, power down the retimer */
	else {
		pi3dpx1207_shutoff_power(port);
		return EC_SUCCESS;
	}

	/* Write the retimer config byte */
	rv = pi3dpx1207_i2c_write(i2c_port, i2c_addr_flags,
				  PI3DPX1207_MODE_OFFSET,
				  mode_val);
	return rv;
}

const struct usb_retimer_driver pi3dpx1207_usb_retimer = {
	.init = pi3dpx1207_init,
	.set = pi3dpx1207_set_mux,
	.enter_low_power_mode = pi3dpx1207_enter_low_power_mode,
};
