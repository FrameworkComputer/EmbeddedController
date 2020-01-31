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

int ps8818_i2c_read(int port, int page, int offset, int *data)
{
	int rv;

	rv = i2c_read8(usb_retimers[port].i2c_port,
		       usb_retimers[port].i2c_addr_flags + page,
		       offset, data);

	if (PS8818_DEBUG)
		ccprintf("%s(%d:0x%02X, 0x%02X) => 0x%02X\n", __func__,
			 usb_retimers[port].i2c_port,
			 usb_retimers[port].i2c_addr_flags + page,
			 offset, *data);

	return rv;
}

int ps8818_i2c_write(int port, int page, int offset, int data)
{
	if (PS8818_DEBUG)
		ccprintf("%s(%d:0x%02X, 0x%02X, 0x%02X)\n", __func__,
			 usb_retimers[port].i2c_port,
			 usb_retimers[port].i2c_addr_flags + page,
			 offset, data);

	return i2c_write8(usb_retimers[port].i2c_port,
			  usb_retimers[port].i2c_addr_flags + page,
			  offset, data);
}

int ps8818_i2c_field_update8(int port, int page, int offset,
			     uint8_t field_mask, uint8_t set_value)
{
	if (PS8818_DEBUG)
		ccprintf("%s(%d:0x%02X, 0x%02X, 0x%02X, 0x%02X)\n", __func__,
			 usb_retimers[port].i2c_port,
			 usb_retimers[port].i2c_addr_flags + page,
			 offset, field_mask, set_value);

	return i2c_field_update8(usb_retimers[port].i2c_port,
				 usb_retimers[port].i2c_addr_flags + page,
				 offset,
				 field_mask,
				 set_value);
}

int ps8818_detect(int port)
{
	int rv = EC_ERROR_NOT_POWERED;
	int val;

	/* Detected if we are powered and can read the device */
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF))
		rv = ps8818_i2c_read(port,
				     PS8818_REG_PAGE0,
				     PS8818_REG0_FLIP,
				     &val);

	return rv;
}

static int ps8818_set_mux(int port, mux_state_t mux_state)
{
	int rv;
	int val = 0;

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return (mux_state == USB_PD_MUX_NONE) ? EC_SUCCESS
						      : EC_ERROR_NOT_POWERED;

	if (PS8818_DEBUG)
		ccprintf("%s(%d, 0x%02X) %s %s %s\n",
			 __func__, port, mux_state,
			 (mux_state & USB_PD_MUX_USB_ENABLED)	? "USB" : "",
			 (mux_state & USB_PD_MUX_DP_ENABLED)	? "DP" : "",
			 (mux_state & USB_PD_MUX_POLARITY_INVERTED)
								? "FLIP" : "");

	/* Set the mode */
	if (mux_state & USB_PD_MUX_USB_ENABLED)
		val |= PS8818_MODE_USB_ENABLE;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		val |= PS8818_MODE_DP_ENABLE;

	rv = ps8818_i2c_field_update8(port,
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

	rv = ps8818_i2c_field_update8(port,
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

	rv = ps8818_i2c_field_update8(port,
				PS8818_REG_PAGE0,
				PS8818_REG0_DPHPD_CONFIG,
				PS8818_DPHPD_NON_RESERVED_MASK,
				val);
	if (rv)
		return rv;

	/* Board specific retimer mux tuning */
	if (usb_retimers[port].tune) {
		rv = usb_retimers[port].tune(port, mux_state);
		if (rv)
			return rv;
	}

	if (PS8818_DEBUG) {
		int tx_status;
		int rx_status;

		rv = ps8818_i2c_read(port,
				     PS8818_REG_PAGE2,
				     PS8818_REG2_TX_STATUS,
				     &tx_status);
		if (rv)
			return rv;

		rv = ps8818_i2c_read(port,
				     PS8818_REG_PAGE2,
				     PS8818_REG2_RX_STATUS,
				     &rx_status);
		if (rv)
			return rv;

		ccprintf("%s: tx:channel %snormal %s10Gbps\n",
			 __func__,
			 (tx_status & PS8818_STATUS_NORMAL_OPERATION)
								? "" : "NOT-",
			 (tx_status & PS8818_STATUS_10_GBPS)	? "" : "NON-");
		ccprintf("%s: rx:channel %snormal %s10Gbps\n",
			 __func__,
			 (rx_status & PS8818_STATUS_NORMAL_OPERATION)
								? "" : "NOT-",
			 (rx_status & PS8818_STATUS_10_GBPS)	? "" : "NON-");
	}

	return rv;
}

const struct usb_retimer_driver ps8818_usb_retimer = {
	.set = ps8818_set_mux,
};
