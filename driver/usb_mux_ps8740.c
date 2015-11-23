/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8740 USB port switch driver.
 */

#include "common.h"
#include "i2c.h"
#include "ps8740.h"
#include "usb_mux.h"
#include "util.h"

static int ps8740_read(int i2c_addr, uint8_t reg, uint8_t *val)
{
	int read, res;

	res = i2c_read8(I2C_PORT_USB_MUX, i2c_addr, reg, &read);
	if (res)
		return res;

	*val = read;
	return EC_SUCCESS;
}

static int ps8740_write(int i2c_addr, uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_USB_MUX, i2c_addr, reg, val);
}

static int ps8740_reset(int i2c_addr)
{
	return ps8740_write(i2c_addr, PS8740_REG_MODE, PS8740_MODE_POWER_DOWN);
}

static int ps8740_init(int i2c_addr)
{
	uint8_t val;
	int res;

	/* Reset chip back to power-on state */
	res = ps8740_reset(i2c_addr);
	if (res)
		return res;

	/* Verify revision / chip ID registers */
	res = ps8740_read(i2c_addr, PS8740_REG_REVISION_ID1, &val);
	if (res)
		return res;
	if (val != PS8740_REVISION_ID1)
		return EC_ERROR_UNKNOWN;

	res = ps8740_read(i2c_addr, PS8740_REG_REVISION_ID2, &val);
	if (res)
		return res;
	if (val < PS8740_REVISION_ID2)
		return EC_ERROR_UNKNOWN;

	res = ps8740_read(i2c_addr, PS8740_REG_CHIP_ID1, &val);
	if (res)
		return res;
	if (val != PS8740_CHIP_ID1)
		return EC_ERROR_UNKNOWN;

	res  = ps8740_read(i2c_addr, PS8740_REG_CHIP_ID2, &val);
	if (res)
		return res;
	if (val != PS8740_CHIP_ID2)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/* Writes control register to set switch mode */
static int ps8740_set_mux(int i2c_addr, mux_state_t mux_state)
{
	uint8_t reg = 0;

	if (mux_state & MUX_USB_ENABLED)
		reg |= PS8740_MODE_USB_ENABLED;
	if (mux_state & MUX_DP_ENABLED)
		reg |= PS8740_MODE_DP_ENABLED;
	if (mux_state & MUX_POLARITY_INVERTED)
		reg |= PS8740_MODE_POLARITY_INVERTED;

	return ps8740_write(i2c_addr, PS8740_REG_MODE, reg);
}

/* Reads control register and updates mux_state accordingly */
static int ps8740_get_mux(int i2c_addr, mux_state_t *mux_state)
{
	uint8_t reg;
	int res;

	*mux_state = 0;
	res = ps8740_read(i2c_addr, PS8740_REG_STATUS, &reg);
	if (res)
		return res;

	if (reg & PS8740_STATUS_USB_ENABLED)
		*mux_state |= MUX_USB_ENABLED;
	if (reg & PS8740_STATUS_DP_ENABLED)
		*mux_state |= MUX_DP_ENABLED;
	if (reg & PS8740_STATUS_POLARITY_INVERTED)
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

/* Tune USB Tx/Rx Equalization */
int ps8740_tune_usb_eq(int i2c_addr, uint8_t tx, uint8_t rx)
{
	int ret = 0;

	ret |= ps8740_write(i2c_addr, PS8740_REG_USB_EQ_TX, tx);
	ret |= ps8740_write(i2c_addr, PS8740_REG_USB_EQ_RX, rx);

	return ret;
}

const struct usb_mux_driver ps8740_usb_mux_driver = {
	.init = ps8740_init,
	.set = ps8740_set_mux,
	.get = ps8740_get_mux,
};
