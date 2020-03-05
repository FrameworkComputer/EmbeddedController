/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ON Semiconductor NB7V904M USB Type-C DisplayPort Alt Mode Redriver
 */
#include <stdbool.h>
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "i2c.h"
#include "nb7v904m.h"
#include "usb_mux.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

static int nb7v904m_write(const struct usb_mux *me, int offset, int data)
{
	return i2c_write8(me->i2c_port,
			  me->i2c_addr_flags,
			  offset, data);

}

static int nb7v904m_read(const struct usb_mux *me, int offset, int *regval)
{
	return i2c_read8(me->i2c_port,
			 me->i2c_addr_flags,
			 offset, regval);

}

static int set_low_power_mode(const struct usb_mux *me, bool enable)
{
	int regval;
	int rv;

	rv = nb7v904m_read(me, NB7V904M_REG_GEN_DEV_SETTINGS, &regval);
	if (rv)
		return rv;

	if (enable)
		regval &= ~NB7V904M_CHIP_EN;
	else
		regval |= NB7V904M_CHIP_EN;

	return nb7v904m_write(me, NB7V904M_REG_GEN_DEV_SETTINGS, regval);
}

static int nb7v904m_enter_low_power_mode(const struct usb_mux *me)
{
	int rv = set_low_power_mode(me, 1);

	if (rv)
		CPRINTS("C%d: NB7V904M: Failed to enter low power mode!",
			me->usb_port);
	return rv;
}

static int nb7v904m_init(const struct usb_mux *me)
{
	int rv = set_low_power_mode(me, 0);

	if (rv)
		CPRINTS("C%d: NB7V904M: init failed!", me->usb_port);
	return rv;
}

static int nb7v904m_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	int regval;
	int flipped = !!(mux_state & USB_PD_MUX_POLARITY_INVERTED);

	/* Turn off redriver if it's not needed at all. */
	if (mux_state == USB_PD_MUX_NONE)
		return nb7v904m_enter_low_power_mode(me);

	rv = nb7v904m_init(me);
	if (rv)
		return rv;

	/* Clear operation mode field */
	rv = nb7v904m_read(me, NB7V904M_REG_GEN_DEV_SETTINGS, &regval);
	if (rv) {
		CPRINTS("C%d %s: Failed to obtain dev settings!",
			me->usb_port, __func__);
		return rv;
	}
	regval &= ~NB7V904M_OP_MODE_MASK;

	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* USB with DP */
		if (mux_state & USB_PD_MUX_DP_ENABLED) {
			if (flipped)
				regval |= NB7V904M_USB_DP_FLIPPED;
			else
				regval |= NB7V904M_USB_DP_NORMAL;
		} else {
			/* USB only */
			regval |= NB7V904M_USB_ONLY;
		}

	} else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* 4 lanes DP */
		regval |= NB7V904M_DP_ONLY;
	}

	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Connect AUX */
		rv = nb7v904m_write(me, NB7V904M_REG_AUX_CH_CTRL, flipped ?
				    NB7V904M_AUX_CH_FLIPPED :
				    NB7V904M_AUX_CH_NORMAL);
		/* Enable all channels for DP */
		regval |= NB7V904M_CH_EN_MASK;
	} else {
		/* Disconnect AUX since it's not being used. */
		rv = nb7v904m_write(me, NB7V904M_REG_AUX_CH_CTRL,
				    NB7V904M_AUX_CH_HI_Z);

		/* Disable the unused channels to save power */
		regval &= ~NB7V904M_CH_EN_MASK;
		if (flipped) {
			/* Only enable channels A & B */
			regval |= NB7V904M_CH_A_EN | NB7V904M_CH_B_EN;
		} else {
			/* Only enable channels C & D */
			regval |= NB7V904M_CH_C_EN | NB7V904M_CH_D_EN;
		}
	}

	rv |= nb7v904m_write(me, NB7V904M_REG_GEN_DEV_SETTINGS, regval);
	if (rv)
		CPRINTS("C%d: %s failed!", me->usb_port, __func__);

	return rv;
}

const struct usb_mux_driver nb7v904m_usb_redriver_drv = {
	.enter_low_power_mode = &nb7v904m_enter_low_power_mode,
	.init = &nb7v904m_init,
	.set = &nb7v904m_set_mux,
};
