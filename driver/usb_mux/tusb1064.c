/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"
#include "tusb1064.h"
#include "usb_mux.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/*
 * configuration bits which never change in the General Register
 * e.g. REG_GENERAL_DP_EN_CTRL or REG_GENERAL_EQ_OVERRIDE
 */
#define REG_GENERAL_STATIC_BITS REG_GENERAL_EQ_OVERRIDE

static int tusb1064_read(const struct usb_mux *me, uint8_t reg, uint8_t *val)
{
	int buffer = 0xee;
	int res = i2c_read8(me->i2c_port, me->i2c_addr_flags,
			    (int)reg, &buffer);
	*val = buffer;
	return res;
}

static int tusb1064_write(const struct usb_mux *me, uint8_t reg, uint8_t val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags,
			  (int)reg, (int)val);
}

/* Writes control register to set switch mode */
static int tusb1064_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			    bool *ack_required)
{
	int reg = REG_GENERAL_STATIC_BITS;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= REG_GENERAL_CTLSEL_USB3;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= REG_GENERAL_CTLSEL_ANYDP;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= REG_GENERAL_FLIPSEL;

	return tusb1064_write(me, TUSB1064_REG_GENERAL, reg);
}

/* Reads control register and updates mux_state accordingly */
static int tusb1064_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	uint8_t reg;
	int res;

	res = tusb1064_read(me, TUSB1064_REG_GENERAL, &reg);
	if (res)
		return EC_ERROR_INVAL;

	*mux_state = 0;
	if (reg & REG_GENERAL_CTLSEL_USB3)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (reg & REG_GENERAL_CTLSEL_ANYDP)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (reg & REG_GENERAL_FLIPSEL)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

/* Generic driver init function */
static int tusb1064_init(const struct usb_mux *me)
{
	int res;
	uint8_t reg;
	bool unused;

	/* Default to "Floating Pin" DP Equalization */
	reg = TUSB1064_DP1EQ(TUSB1064_DP_EQ_RX_10_0_DB) |
		TUSB1064_DP3EQ(TUSB1064_DP_EQ_RX_10_0_DB);
	res = tusb1064_write(me, TUSB1064_REG_DP1DP3EQ_SEL, reg);
	if (res)
		return res;

	reg = TUSB1064_DP0EQ(TUSB1064_DP_EQ_RX_10_0_DB) |
		TUSB1064_DP2EQ(TUSB1064_DP_EQ_RX_10_0_DB);
	res = tusb1064_write(me, TUSB1064_REG_DP0DP2EQ_SEL, reg);
	if (res)
		return res;

	/*
	 * Note that bypassing the usb_mux API is okay for internal driver calls
	 * since the task calling init already holds this port's mux lock.
	 */
	/* Disconnect USB3.1 and DP */
	res = tusb1064_set_mux(me, USB_PD_MUX_NONE, &unused);
	if (res)
		return res;

	/* Disable AUX mux override */
	res = tusb1064_write(me, TUSB1064_REG_AUXDPCTRL, 0);
	if (res)
		return res;

	return EC_SUCCESS;
}

const struct usb_mux_driver tusb1064_usb_mux_driver = {
	/* CAUTION: This is an UFP/RX/SINK redriver mux */
	.init = tusb1064_init,
	.set = tusb1064_set_mux,
	.get = tusb1064_get_mux,
};
