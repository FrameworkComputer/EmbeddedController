/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8743 USB Type-C Redriving Switch for USB Host / DisplayPort.
 *
 * TODO: Merge PS8743 & PS8740 as PS874X as both the drivers are almost same.
 */

#include "common.h"
#include "i2c.h"
#include "ps8743.h"
#include "usb_mux.h"
#include "util.h"

static inline int ps8743_read(int i2c_addr, uint8_t reg, int *val)
{
	return i2c_read8(I2C_PORT_USB_MUX, i2c_addr, reg, val);
}

static inline int ps8743_write(int i2c_addr, uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_USB_MUX, i2c_addr, reg, val);
}

static int ps8743_init(int i2c_addr)
{
	int val;
	int res;

	/* Reset chip back to power-on state */
	res = ps8743_write(i2c_addr, PS8743_REG_MODE, PS8743_MODE_POWER_DOWN);
	if (res)
		return res;

	/*
	 * Verify revision / chip ID registers.
	 * From Parade: PS8743 may have REVISION_ID1 as 0 or 1,
	 * 1 is derived from 0 and have same functionality.
	 */
	res = ps8743_read(i2c_addr, PS8743_REG_REVISION_ID1, &val);
	if (res)
		return res;
	if (val <= PS8743_REVISION_ID1)
		return EC_ERROR_UNKNOWN;

	res = ps8743_read(i2c_addr, PS8743_REG_REVISION_ID2, &val);
	if (res)
		return res;
	if (val != PS8743_REVISION_ID2)
		return EC_ERROR_UNKNOWN;

	res = ps8743_read(i2c_addr, PS8743_REG_CHIP_ID1, &val);
	if (res)
		return res;
	if (val != PS8743_CHIP_ID1)
		return EC_ERROR_UNKNOWN;

	res  = ps8743_read(i2c_addr, PS8743_REG_CHIP_ID2, &val);
	if (res)
		return res;
	if (val != PS8743_CHIP_ID2)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/* Writes control register to set switch mode */
static int ps8743_set_mux(int i2c_addr, mux_state_t mux_state)
{
	uint8_t reg = 0;

	if (mux_state & MUX_USB_ENABLED)
		reg |= PS8743_MODE_USB_ENABLED;
	if (mux_state & MUX_DP_ENABLED)
		reg |= PS8743_MODE_DP_ENABLED;
	if (mux_state & MUX_POLARITY_INVERTED)
		reg |= PS8743_MODE_POLARITY_INVERTED;

	return ps8743_write(i2c_addr, PS8743_REG_MODE, reg);
}

/* Reads control register and updates mux_state accordingly */
static int ps8743_get_mux(int i2c_addr, mux_state_t *mux_state)
{
	int reg;
	int res;

	res = ps8743_read(i2c_addr, PS8743_REG_STATUS, &reg);
	if (res)
		return res;

	*mux_state = 0;
	if (reg & PS8743_STATUS_USB_ENABLED)
		*mux_state |= MUX_USB_ENABLED;
	if (reg & PS8743_STATUS_DP_ENABLED)
		*mux_state |= MUX_DP_ENABLED;
	if (reg & PS8743_STATUS_POLARITY_INVERTED)
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

const struct usb_mux_driver ps8743_usb_mux_driver = {
	.init = ps8743_init,
	.set = ps8743_set_mux,
	.get = ps8743_get_mux,
};
