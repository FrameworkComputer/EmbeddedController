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

#define PS8818_DEBUG 0

int ps8818_i2c_read(const struct usb_mux *me, int page, int offset, int *data)
{
	int rv;

	rv = i2c_read8(me->i2c_port,
		       me->i2c_addr_flags + page,
		       offset, data);

	if (PS8818_DEBUG)
		ccprintf("%s(%d:0x%02X, 0x%02X) =>0x%02X\n", __func__,
			 me->usb_port,
			 me->i2c_addr_flags + page,
			 offset, *data);

	return rv;
}

int ps8818_i2c_write(const struct usb_mux *me, int page, int offset, int data)
{
	int rv;
	int pre_val, post_val;

	if (PS8818_DEBUG)
		i2c_read8(me->i2c_port,
			me->i2c_addr_flags + page,
			offset, &pre_val);

	rv = i2c_write8(me->i2c_port,
			me->i2c_addr_flags + page,
			offset, data);

	if (PS8818_DEBUG) {
		i2c_read8(me->i2c_port,
			me->i2c_addr_flags + page,
			offset, &post_val);

		ccprintf("%s(%d:0x%02X, 0x%02X, 0x%02X) "
			"0x%02X=>0x%02X\n",
			 __func__,
			 me->usb_port,
			 me->i2c_addr_flags + page,
			 offset, data,
			 pre_val, post_val);
	}

	return rv;
}

int ps8818_i2c_field_update8(const struct usb_mux *me, int page, int offset,
			     uint8_t field_mask, uint8_t set_value)
{
	int rv;
	int pre_val, post_val;

	if (PS8818_DEBUG)
		i2c_read8(me->i2c_port,
			me->i2c_addr_flags + page,
			offset, &pre_val);

	rv = i2c_field_update8(me->i2c_port,
			       me->i2c_addr_flags + page,
			       offset,
			       field_mask,
			       set_value);

	if (PS8818_DEBUG) {
		i2c_read8(me->i2c_port,
			me->i2c_addr_flags + page,
			offset, &post_val);

		ccprintf("%s(%d:0x%02X, 0x%02X, 0x%02X, 0x%02X) "
			 "0x%02X=>0x%02X\n",
			 __func__,
			 me->usb_port,
			 me->i2c_addr_flags + page,
			 offset, field_mask, set_value,
			 pre_val, post_val);
	}

	return rv;
}

static int ps8818_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv;
	int val = 0;

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return (mux_state == USB_PD_MUX_NONE) ? EC_SUCCESS
						      : EC_ERROR_NOT_POWERED;

	if (PS8818_DEBUG)
		ccprintf("%s(%d, 0x%02X) %s %s %s\n",
			 __func__, me->usb_port, mux_state,
			 (mux_state & USB_PD_MUX_USB_ENABLED)	? "USB" : "",
			 (mux_state & USB_PD_MUX_DP_ENABLED)	? "DP" : "",
			 (mux_state & USB_PD_MUX_POLARITY_INVERTED)
								? "FLIP" : "");

	/* Set the mode */
	if (mux_state & USB_PD_MUX_USB_ENABLED)
		val |= PS8818_MODE_USB_ENABLE;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		val |= PS8818_MODE_DP_ENABLE;

	rv = ps8818_i2c_field_update8(me,
				PS8818_REG_PAGE0,
				PS8818_REG0_MODE,
				PS8818_MODE_NON_RESERVED_MASK,
				val);
	if (rv)
		return rv;

	/* Set the flip */
	val = 0;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		val |= PS8818_FLIP_CONFIG;

	rv = ps8818_i2c_field_update8(me,
				PS8818_REG_PAGE0,
				PS8818_REG0_FLIP,
				PS8818_FLIP_NON_RESERVED_MASK,
				val);
	if (rv)
		return rv;

	/* Set the IN_HPD */
	val = PS8818_DPHPD_CONFIG_INHPD_DISABLE;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		val |= PS8818_DPHPD_PLUGGED;

	rv = ps8818_i2c_field_update8(me,
				PS8818_REG_PAGE0,
				PS8818_REG0_DPHPD_CONFIG,
				PS8818_DPHPD_NON_RESERVED_MASK,
				val);

	return rv;
}

const struct usb_mux_driver ps8818_usb_retimer_driver = {
	.set = ps8818_set_mux,
};
