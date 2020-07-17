/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PS8802 retimer.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "ps8802.h"
#include "timer.h"
#include "usb_mux.h"

#define PS8802_DEBUG 0
#define PS8802_I2C_WAKE_DELAY 500

int ps8802_i2c_read(const struct usb_mux *me, int page, int offset, int *data)
{
	int rv;

	rv = i2c_read8(me->i2c_port,
		       me->i2c_addr_flags + page,
		       offset, data);

	if (PS8802_DEBUG)
		ccprintf("%s(%d:0x%02X, 0x%02X) =>0x%02X\n", __func__,
			 me->i2c_port,
			 me->i2c_addr_flags + page,
			 offset, *data);

	return rv;
}

int ps8802_i2c_write(const struct usb_mux *me, int page, int offset, int data)
{
	int rv;
	int pre_val, post_val;

	if (PS8802_DEBUG)
		i2c_read8(me->i2c_port,
			me->i2c_addr_flags + page,
			offset, &pre_val);

	rv = i2c_write8(me->i2c_port,
			me->i2c_addr_flags + page,
			offset, data);

	if (PS8802_DEBUG) {
		i2c_read8(me->i2c_port,
			me->i2c_addr_flags + page,
			offset, &post_val);

		ccprintf("%s(%d:0x%02X, 0x%02X, 0x%02X) "
			"0x%02X=>0x%02X\n",
			 __func__,
			 me->i2c_port,
			 me->i2c_addr_flags + page,
			 offset, data,
			 pre_val, post_val);
	}

	return rv;
}

int ps8802_i2c_write16(const struct usb_mux *me, int page, int offset,
		       int data)
{
	int rv;
	int pre_val, post_val;

	if (PS8802_DEBUG)
		i2c_read16(me->i2c_port,
			   me->i2c_addr_flags + page,
			   offset, &pre_val);

	rv = i2c_write16(me->i2c_port,
			 me->i2c_addr_flags + page,
			 offset, data);

	if (PS8802_DEBUG) {
		i2c_read16(me->i2c_port,
			   me->i2c_addr_flags + page,
			   offset, &post_val);

		ccprintf("%s(%d:0x%02X, 0x%02X, 0x%04X) "
			 "0x%04X=>0x%04X\n",
			 __func__,
			 me->i2c_port,
			 me->i2c_addr_flags + page,
			 offset, data,
			 pre_val, post_val);
	}

	return rv;
}

int ps8802_i2c_field_update8(const struct usb_mux *me, int page, int offset,
			     uint8_t field_mask, uint8_t set_value)
{
	int rv;
	int pre_val, post_val;

	if (PS8802_DEBUG)
		i2c_read8(me->i2c_port,
			  me->i2c_addr_flags + page,
			  offset, &pre_val);

	rv = i2c_field_update8(me->i2c_port,
			       me->i2c_addr_flags + page,
			       offset,
			       field_mask,
			       set_value);

	if (PS8802_DEBUG) {
		i2c_read8(me->i2c_port,
			  me->i2c_addr_flags + page,
			  offset, &post_val);

		ccprintf("%s(%d:0x%02X, 0x%02X, 0x%02X, 0x%02X) "
			 "0x%02X=>0x%02X\n",
			 __func__,
			 me->i2c_port,
			 me->i2c_addr_flags + page,
			 offset, field_mask, set_value,
			 pre_val, post_val);
	}

	return rv;
}

int ps8802_i2c_field_update16(const struct usb_mux *me, int page, int offset,
			     uint16_t field_mask, uint16_t set_value)
{
	int rv;
	int pre_val, post_val;

	if (PS8802_DEBUG)
		i2c_read16(me->i2c_port,
			   me->i2c_addr_flags + page,
			   offset, &pre_val);

	rv = i2c_field_update16(me->i2c_port,
				me->i2c_addr_flags + page,
				offset,
				field_mask,
				set_value);

	if (PS8802_DEBUG) {
		i2c_read16(me->i2c_port,
			   me->i2c_addr_flags + page,
			   offset, &post_val);

		ccprintf("%s(%d:0x%02X, 0x%02X, 0x%02X, 0x%04X) "
			 "0x%04X=>0x%04X\n",
			 __func__,
			 me->i2c_port,
			 me->i2c_addr_flags + page,
			 offset, field_mask, set_value,
			 pre_val, post_val);
	}

	return rv;
}

/*
 * If PS8802 is in I2C standby mode, wake it up by reading PS8802_REG_MODE.
 * From Application Note: 1) Activate by reading any Page 2 register. 2) Wait
 * 500 microseconds. 3) After 5 seconds idle, PS8802 will return to standby.
 */
int ps8802_i2c_wake(const struct usb_mux *me)
{
	int data;
	int rv = EC_ERROR_UNKNOWN;

	/* If in standby, first read will fail, second should succeed. */
	for (int i = 0; i < 2; i++) {
		rv = ps8802_i2c_read(me,
				     PS8802_REG_PAGE2,
				     PS8802_REG2_MODE,
				     &data);
		if (rv == EC_SUCCESS)
			return rv;

		usleep(PS8802_I2C_WAKE_DELAY);
	}

	return rv;
}

static int ps8802_init(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

static int ps8802_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	int val;
	int rv;

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return (mux_state == USB_PD_MUX_NONE) ? EC_SUCCESS
						      : EC_ERROR_NOT_POWERED;

	/* Make sure the PS8802 is awake */
	rv = ps8802_i2c_wake(me);
	if (rv)
		return rv;

	if (PS8802_DEBUG)
		ccprintf("%s(%d, 0x%02X) %s %s %s\n",
			 __func__, me->usb_port, mux_state,
			 (mux_state & USB_PD_MUX_USB_ENABLED)	? "USB" : "",
			 (mux_state & USB_PD_MUX_DP_ENABLED)	? "DP" : "",
			 (mux_state & USB_PD_MUX_POLARITY_INVERTED)
								? "FLIP" : "");

	/* Set the mode and flip */
	val = (PS8802_MODE_DP_REG_CONTROL |
	       PS8802_MODE_USB_REG_CONTROL |
	       PS8802_MODE_FLIP_REG_CONTROL |
	       PS8802_MODE_IN_HPD_REG_CONTROL);

	if (mux_state & USB_PD_MUX_USB_ENABLED)
		val |= PS8802_MODE_USB_ENABLE;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		val |= PS8802_MODE_DP_ENABLE | PS8802_MODE_IN_HPD_ENABLE;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		val |= PS8802_MODE_FLIP_ENABLE;

	rv = ps8802_i2c_write(me,
			      PS8802_REG_PAGE2,
			      PS8802_REG2_MODE,
			      val);

	return rv;
}

static int ps8802_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int rv;
	int val;

	*mux_state = USB_PD_MUX_NONE;

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	rv = ps8802_i2c_wake(me);
	if (rv)
		return rv;

	rv = ps8802_i2c_read(me,
			     PS8802_REG_PAGE2,
			     PS8802_REG2_MODE,
			     &val);
	if (rv)
		return rv;

	if (val & PS8802_MODE_USB_ENABLE)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (val & PS8802_MODE_DP_ENABLE)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (val & PS8802_MODE_FLIP_ENABLE)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return rv;
}

const struct usb_mux_driver ps8802_usb_mux_driver = {
	.init = ps8802_init,
	.set = ps8802_set_mux,
	.get = ps8802_get_mux,
};
