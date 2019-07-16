/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Pericom PI3USB30532 USB port switch driver.
 */

#include "common.h"
#include "i2c.h"
#include "pi3usb30532.h"
#include "usb_mux.h"
#include "util.h"

static int pi3usb30532_read(int port, uint8_t reg, uint8_t *val)
{
	int read, res;

	/*
	 * First byte read will be slave address (ignored).
	 * Second byte read will be vendor ID.
	 * Third byte read will be selection control.
	 */
	res = i2c_read16(I2C_PORT_USB_MUX, MUX_ADDR(port),
			 0, &read);
	if (res)
		return res;

	if (reg == PI3USB30532_REG_VENDOR)
		*val = read & 0xff;
	else /* reg == PI3USB30532_REG_CONTROL */
		*val = (read >> 8) & 0xff;

	return EC_SUCCESS;
}

static int pi3usb30532_write(int port, uint8_t reg, uint8_t val)
{
	if (reg != PI3USB30532_REG_CONTROL)
		return EC_ERROR_UNKNOWN;

	return i2c_write8(I2C_PORT_USB_MUX, MUX_ADDR(port),
			  0, val);
}

static int pi3usb30532_reset(int port)
{
	return pi3usb30532_write(
		port,
		PI3USB30532_REG_CONTROL,
		(PI3USB30532_MODE_POWERDOWN & PI3USB30532_CTRL_MASK) |
		PI3USB30532_CTRL_RSVD);
}

static int pi3usb30532_init(int port)
{
	uint8_t val;
	int res;

	res = pi3usb30532_reset(port);
	if (res)
		return res;
	res = pi3usb30532_read(port, PI3USB30532_REG_VENDOR, &val);
	if (res)
		return res;
	if (val != PI3USB30532_VENDOR_ID)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/* Writes control register to set switch mode */
static int pi3usb30532_set_mux(int port, mux_state_t mux_state)
{
	uint8_t reg = 0;

	if (mux_state & MUX_USB_ENABLED)
		reg |= PI3USB30532_MODE_USB;
	if (mux_state & MUX_DP_ENABLED)
		reg |= PI3USB30532_MODE_DP;
	if (mux_state & MUX_POLARITY_INVERTED)
		reg |= PI3USB30532_BIT_SWAP;

	return pi3usb30532_write(port, PI3USB30532_REG_CONTROL,
				 reg | PI3USB30532_CTRL_RSVD);
}

/* Reads control register and updates mux_state accordingly */
static int pi3usb30532_get_mux(int port, mux_state_t *mux_state)
{
	uint8_t reg = 0;
	uint8_t res;

	*mux_state = 0;
	res = pi3usb30532_read(port, PI3USB30532_REG_CONTROL, &reg);
	if (res)
		return res;

	if (reg & PI3USB30532_MODE_USB)
		*mux_state |= MUX_USB_ENABLED;
	if (reg & PI3USB30532_MODE_DP)
		*mux_state |= MUX_DP_ENABLED;
	if (reg & PI3USB30532_BIT_SWAP)
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

const struct usb_mux_driver pi3usb30532_usb_mux_driver = {
	.init = pi3usb30532_init,
	.set = pi3usb30532_set_mux,
	.get = pi3usb30532_get_mux,
};
