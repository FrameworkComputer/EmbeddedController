/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"
#include "tusb1064.h"
#include "usb_mux.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#if defined(CONFIG_USB_MUX_TUSB1044) && defined(CONFIG_USB_MUX_TUSB1064)
#error "Must choose CONFIG_USB_MUX_TUSB1044 or CONFIG_USB_MUX_TUSB1064"
#endif

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

#if defined(CONFIG_USB_MUX_TUSB1044)
void tusb1044_hpd_update(const struct usb_mux *me, mux_state_t mux_state)
{
	int res;
	uint8_t reg;

	res = tusb1064_read(me, TUSB1064_REG_GENERAL, &reg);
	if (res)
		return;

	/*
	 *  Overrides HPDIN pin state.
		Settings of this bit will enable the Display port lanes.
		0h = HPD_IN based on HPD_IN pin.
		1h = HPD_IN high.
	 */
	if (mux_state & USB_PD_MUX_HPD_LVL)
		reg |= REG_GENERAL_HPDIN_OVERRIDE;
	else
		reg &= ~REG_GENERAL_HPDIN_OVERRIDE;

	tusb1064_write(me, TUSB1064_REG_GENERAL, reg);
}
#endif

int tusb1064_set_dp_rx_eq(const struct usb_mux *me, int db)
{
	uint8_t reg;
	int rv;

	if (db < TUSB1064_DP_EQ_RX_NEG_0_3_DB || db > TUSB1064_DP_EQ_RX_12_1_DB)
		return EC_ERROR_INVAL;

	/* Set the requested gain values */
	reg = TUSB1064_DP1EQ(db) | TUSB1064_DP3EQ(db);
	rv = tusb1064_write(me, TUSB1064_REG_DP1DP3EQ_SEL, reg);
	if (rv)
		return rv;

	reg = TUSB1064_DP0EQ(db) | TUSB1064_DP2EQ(db);
	rv = tusb1064_write(me, TUSB1064_REG_DP0DP2EQ_SEL, reg);
	if (rv)
		return rv;

	/* Enable EQ_OVERRIDE so the gain registers are used */
	rv = tusb1064_read(me, TUSB1064_REG_GENERAL, &reg);
	if (rv)
		return rv;

	reg |= REG_GENERAL_EQ_OVERRIDE;

	return tusb1064_write(me, TUSB1064_REG_GENERAL, reg);
}

/* Writes control register to set switch mode */
static int tusb1064_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			    bool *ack_required)
{
	uint8_t reg;
	int rv;
	int mask;

	rv = tusb1064_read(me, TUSB1064_REG_GENERAL, &reg);
	if (rv)
		return rv;

	/* Mask bits that may be set in this function */
	mask = REG_GENERAL_CTLSEL_USB3 | REG_GENERAL_CTLSEL_ANYDP |
		REG_GENERAL_FLIPSEL;
#ifdef CONFIG_USB_MUX_TUSB1044
	mask |= REG_GENERAL_HPDIN_OVERRIDE;
#endif
	reg &= ~mask;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= REG_GENERAL_CTLSEL_USB3;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= REG_GENERAL_CTLSEL_ANYDP;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= REG_GENERAL_FLIPSEL;
#if defined(CONFIG_USB_MUX_TUSB1044)
	if (mux_state & USB_PD_MUX_HPD_LVL)
		reg |= REG_GENERAL_HPDIN_OVERRIDE;
#endif

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
#if defined(CONFIG_USB_MUX_TUSB1044)
	if (reg & REG_GENERAL_HPDIN_OVERRIDE)
		*mux_state |= USB_PD_MUX_HPD_LVL;
#endif

	return EC_SUCCESS;
}

/* Generic driver init function */
static int tusb1064_init(const struct usb_mux *me)
{
	int res;
	bool unused;

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
