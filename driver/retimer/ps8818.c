/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PS8818 retimer.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "ioexpander.h"
#include "ps8818.h"
#include "usb_mux.h"

static int ps8818_i2c_read(int port, int offset, int *data)
{
	return i2c_read8(usb_retimers[port].i2c_port,
			 usb_retimers[port].i2c_addr_flags,
			 offset, data);
}

static int ps8818_i2c_write(int port, int offset, int data)
{
	return i2c_write8(usb_retimers[port].i2c_port,
			  usb_retimers[port].i2c_addr_flags,
			  offset, data);
}

int ps8818_detect(int port)
{
	int rv = EC_ERROR_NOT_POWERED;
	int val;

	/* Detected if we are powered and can read the device */
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF))
		rv = ps8818_i2c_read(port, PS8818_REG_FLIP, &val);

	return rv;
}

static int ps8818_set_mux(int port, mux_state_t mux_state)
{
	int rv;
	int val = 0;

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return (mux_state == USB_PD_MUX_NONE) ? EC_SUCCESS
						     : EC_ERROR_NOT_POWERED;

	if (mux_state & USB_PD_MUX_USB_ENABLED)
		val |= PS8818_MODE_USB_ENABLE;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		val |= PS8818_MODE_DP_ENABLE;

	rv = ps8818_i2c_write(port, PS8818_REG_MODE, val);
	if (rv)
		return rv;

	val = 0;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		val |= PS8818_FLIP_CONFIG;

	return ps8818_i2c_write(port, PS8818_REG_FLIP, val);
}

const struct usb_retimer_driver ps8818_usb_retimer = {
	.set = ps8818_set_mux,
};
