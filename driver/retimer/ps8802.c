/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PS8802 retimer.
 */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "ps8802.h"
#include "timer.h"
#include "usb_mux.h"

#define PS8802_I2C_WAKE_DELAY 500

/*
 * If PS8802 is in I2C standby mode, wake it up by reading PS8802_REG_MODE.
 * From Application Note: 1) Activate by reading any Page 2 register. 2) Wait
 * 500 microseconds. 3) After 5 seconds idle, PS8802 will return to standby.
 */
static int ps8802_i2c_wake(int port)
{
	int data;
	int rv = EC_ERROR_UNKNOWN;

	/* If in standby, first read will fail, second should succeed. */
	for (int i = 0; i < 2; i++) {
		rv = i2c_read8(usb_retimers[port].i2c_port,
			       usb_retimers[port].i2c_addr_flags,
			       PS8802_REG_MODE, &data);
		if (rv == EC_SUCCESS)
			return rv;

		usleep(PS8802_I2C_WAKE_DELAY);
	}

	return rv;
}

static int ps8802_i2c_write(int port, int offset, int data)
{
	return i2c_write8(usb_retimers[port].i2c_port,
			  usb_retimers[port].i2c_addr_flags,
			  offset, data);
}

static int ps8802_set_mux(int port, mux_state_t mux_state)
{
	int val = (PS8802_MODE_DP_REG_CONTROL
		   | PS8802_MODE_USB_REG_CONTROL
		   | PS8802_MODE_FLIP_REG_CONTROL
		   | PS8802_MODE_IN_HPD_REG_CONTROL);
	int rv;

	rv = ps8802_i2c_wake(port);
	if (rv)
		return rv;

	if (mux_state & MUX_USB_ENABLED)
		val |= PS8802_MODE_USB_ENABLE;
	if (mux_state & MUX_DP_ENABLED)
		val |= PS8802_MODE_DP_ENABLE;
	if (mux_state & MUX_POLARITY_INVERTED)
		val |= PS8802_MODE_FLIP_ENABLE;

	return ps8802_i2c_write(port, PS8802_REG_MODE, val);
}

const struct usb_retimer_driver ps8802_usb_retimer = {
	.set = ps8802_set_mux,
};
