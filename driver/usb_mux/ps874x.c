/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS874X USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#include "common.h"
#include "i2c.h"
#include "ps874x.h"
#include "usb_mux.h"
#include "util.h"

static inline int ps874x_read(int port, uint8_t reg, int *val)
{
	return i2c_read8(I2C_PORT_USB_MUX, MUX_ADDR(port),
			 reg, val);
}

static inline int ps874x_write(int port, uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_USB_MUX, MUX_ADDR(port),
			  reg, val);
}

static int ps874x_init(int port)
{
	int val;
	int res;

	/* Reset chip back to power-on state */
	res = ps874x_write(port, PS874X_REG_MODE, PS874X_MODE_POWER_DOWN);
	if (res)
		return res;

	/*
	 * Verify revision / chip ID registers.
	 */
	res = ps874x_read(port, PS874X_REG_REVISION_ID1, &val);
	if (res)
		return res;

#ifdef CONFIG_USB_MUX_PS8743
	/*
	 * From Parade: PS8743 may have REVISION_ID1 as 0 or 1
	 * Rev 1 is derived from Rev 0 and have same functionality.
	 */
	if (val != PS874X_REVISION_ID1_0 && val != PS874X_REVISION_ID1_1)
		return EC_ERROR_UNKNOWN;
#else
	if (val != PS874X_REVISION_ID1)
		return EC_ERROR_UNKNOWN;
#endif

	res = ps874x_read(port, PS874X_REG_REVISION_ID2, &val);
	if (res)
		return res;
	if (val != PS874X_REVISION_ID2)
		return EC_ERROR_UNKNOWN;

	res = ps874x_read(port, PS874X_REG_CHIP_ID1, &val);
	if (res)
		return res;
	if (val != PS874X_CHIP_ID1)
		return EC_ERROR_UNKNOWN;

	res  = ps874x_read(port, PS874X_REG_CHIP_ID2, &val);
	if (res)
		return res;
	if (val != PS874X_CHIP_ID2)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/* Writes control register to set switch mode */
static int ps874x_set_mux(int port, mux_state_t mux_state)
{
	uint8_t reg = 0;

	if (mux_state & MUX_USB_ENABLED)
		reg |= PS874X_MODE_USB_ENABLED;
	if (mux_state & MUX_DP_ENABLED)
		reg |= PS874X_MODE_DP_ENABLED;
	if (mux_state & MUX_POLARITY_INVERTED)
		reg |= PS874X_MODE_POLARITY_INVERTED;

	return ps874x_write(port, PS874X_REG_MODE, reg);
}

/* Reads control register and updates mux_state accordingly */
static int ps874x_get_mux(int port, mux_state_t *mux_state)
{
	int reg;
	int res;

	res = ps874x_read(port, PS874X_REG_STATUS, &reg);
	if (res)
		return res;

	*mux_state = 0;
	if (reg & PS874X_STATUS_USB_ENABLED)
		*mux_state |= MUX_USB_ENABLED;
	if (reg & PS874X_STATUS_DP_ENABLED)
		*mux_state |= MUX_DP_ENABLED;
	if (reg & PS874X_STATUS_POLARITY_INVERTED)
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

/* Tune USB Tx/Rx Equalization */
int ps874x_tune_usb_eq(int port, uint8_t tx, uint8_t rx)
{
	int ret;

	ret = ps874x_write(port, PS874X_REG_USB_EQ_TX, tx);
	ret |= ps874x_write(port, PS874X_REG_USB_EQ_RX, rx);

	return ret;
}

const struct usb_mux_driver ps874x_usb_mux_driver = {
	.init = ps874x_init,
	.set = ps874x_set_mux,
	.get = ps874x_get_mux,
};
