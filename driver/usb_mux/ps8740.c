/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8740 (and PS8742)
 * USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#include "common.h"
#include "i2c.h"
#include "ps8740.h"
#include "usb_mux.h"
#include "util.h"

int ps8740_read(const struct usb_mux *me, uint8_t reg, int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags,
			 reg, val);
}

int ps8740_write(const struct usb_mux *me, uint8_t reg, uint8_t val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags,
			  reg, val);
}

static int ps8740_init(const struct usb_mux *me)
{
	int id1;
	int id2;
	int res;

	/* Reset chip back to power-on state */
	res = ps8740_write(me, PS8740_REG_MODE, PS8740_MODE_POWER_DOWN);
	if (res)
		return res;

	/*
	 * Verify chip ID registers.
	 */
	res = ps8740_read(me, PS8740_REG_CHIP_ID1, &id1);
	if (res)
		return res;

	res  = ps8740_read(me, PS8740_REG_CHIP_ID2, &id2);
	if (res)
		return res;

	if (id1 != PS8740_CHIP_ID1 || id2 != PS8740_CHIP_ID2)
		return EC_ERROR_UNKNOWN;

	/*
	 * Verify revision ID registers.
	 */
	res = ps8740_read(me, PS8740_REG_REVISION_ID1, &id1);
	if (res)
		return res;

	res = ps8740_read(me, PS8740_REG_REVISION_ID2, &id2);
	if (res)
		return res;

	if (id1 != PS8740_REVISION_ID1)
		return EC_ERROR_UNKNOWN;
	/* PS8740 may have REVISION_ID2 as 0xa or 0xb */
	if (id2 != PS8740_REVISION_ID2_0 && id2 != PS8740_REVISION_ID2_1)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/* Writes control register to set switch mode */
static int ps8740_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	uint8_t reg = 0;

	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= PS8740_MODE_USB_ENABLED;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= PS8740_MODE_DP_ENABLED;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= PS8740_MODE_POLARITY_INVERTED;

	return ps8740_write(me, PS8740_REG_MODE, reg);
}

/* Reads control register and updates mux_state accordingly */
static int ps8740_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int reg;
	int res;

	res = ps8740_read(me, PS8740_REG_STATUS, &reg);
	if (res)
		return res;

	*mux_state = 0;
	if (reg & PS8740_STATUS_USB_ENABLED)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (reg & PS8740_STATUS_DP_ENABLED)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (reg & PS8740_STATUS_POLARITY_INVERTED)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

/* Tune USB Tx/Rx Equalization */
int ps8740_tune_usb_eq(int port, uint8_t tx, uint8_t rx)
{
	int ret;
	const struct usb_mux *me = &usb_muxes[port];

	ret = ps8740_write(me, PS8740_REG_USB_EQ_TX, tx);
	ret |= ps8740_write(me, PS8740_REG_USB_EQ_RX, rx);

	return ret;
}

const struct usb_mux_driver ps8740_usb_mux_driver = {
	.init = ps8740_init,
	.set = ps8740_set_mux,
	.get = ps8740_get_mux,
};
