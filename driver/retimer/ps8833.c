/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8833 USB4 Retimer
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/retimer/ps8833.h"
#include "hooks.h"
#include "i2c.h"
#include "queue.h"
#include "timer.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static int ps8833_read(const struct usb_mux *me, int page, uint8_t reg,
		       int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags + page, reg, val);
}

static int ps8833_write(const struct usb_mux *me, int page, uint8_t reg,
			int val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags + page, reg, val);
}

/* Writes control register to set switch mode */
static int ps8833_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			  bool *ack_required)
{
	int mode;
	int dp;
	int rv;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* This driver treats safe mode as none */
	if (mux_state & USB_PD_MUX_SAFE_MODE)
		mux_state = USB_PD_MUX_NONE;

	rv = ps8833_read(me, PS8833_REG_PAGE0, PS8833_REG_MODE, &mode);
	if (rv) {
		CPRINTSUSB("C%d: PS8833 mode read fail %d", me->usb_port, rv);
		return rv;
	}

	rv = ps8833_read(me, PS8833_REG_PAGE0, PS8833_REG_DP, &dp);
	if (rv) {
		CPRINTSUSB("C%d: PS8833 DP read fail %d", me->usb_port, rv);
		return rv;
	}

	mode &= ~(PS8833_REG_MODE_USB_EN | PS8833_REG_MODE_FLIP |
		  PS8833_REG_MODE_CONN);

	if (pd_is_connected(me->usb_port))
		mode |= PS8833_REG_MODE_CONN;

	/* TODO(b/288635144): figure out why DP alt mode isn't working. */
	dp &= ~PS8833_REG_DP_EN;

	if (mux_state & USB_PD_MUX_USB_ENABLED)
		mode |= PS8833_REG_MODE_USB_EN;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		dp |= PS8833_REG_DP_EN;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		mode |= PS8833_REG_MODE_FLIP;

	rv = ps8833_write(me, PS8833_REG_PAGE0, PS8833_REG_MODE, mode);
	if (rv) {
		CPRINTSUSB("C%d: PS8833 mode write fail %d", me->usb_port, rv);
		return rv;
	}

	rv = ps8833_write(me, PS8833_REG_PAGE0, PS8833_REG_DP, dp);
	if (rv)
		CPRINTSUSB("C%d: PS8833 DP write fail %d", me->usb_port, rv);

	return rv;
}

/* Reads control register and updates mux_state accordingly */
static int ps8833_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int mode;
	int dp;
	int rv;

	rv = ps8833_read(me, PS8833_REG_PAGE0, PS8833_REG_MODE, &mode);
	if (rv) {
		CPRINTSUSB("C%d: PS8833 mode read fail %d", me->usb_port, rv);
		return rv;
	}

	rv = ps8833_read(me, PS8833_REG_PAGE0, PS8833_REG_DP, &dp);
	if (rv) {
		CPRINTSUSB("C%d: PS8833 DP read fail %d", me->usb_port, rv);
		return rv;
	}

	*mux_state = 0;
	if (mode & PS8833_REG_MODE_USB_EN)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (dp & PS8833_REG_DP_EN)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (mode & PS8833_REG_MODE_FLIP)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

const struct usb_mux_driver ps8833_usb_retimer_driver = {
	.set = ps8833_set_mux,
	.get = ps8833_get_mux,
};
