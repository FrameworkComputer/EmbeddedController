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

#define PS8802_I2C_WAKE_DELAY 500

static int ps8802_i2c_read(int port, int offset, int *data)
{
	return i2c_read8(usb_retimers[port].i2c_port,
			 usb_retimers[port].i2c_addr_flags,
			 offset, data);
}

static int ps8802_i2c_write(int port, int offset, int data)
{
	return i2c_write8(usb_retimers[port].i2c_port,
			  usb_retimers[port].i2c_addr_flags,
			  offset, data);
}

/*
 * If PS8802 is in I2C standby mode, wake it up by reading PS8802_REG_MODE.
 * From Application Note: 1) Activate by reading any Page 2 register. 2) Wait
 * 500 microseconds. 3) After 5 seconds idle, PS8802 will return to standby.
 */
static int ps8802_i2c_wake(int port)
{
	int data;
	int rv = EC_ERROR_UNKNOWN;

	/* If in standby, first read will fail, second should succeed. */
	for (int i = 0; i < 2; i++) {
		rv = ps8802_i2c_read(port, PS8802_REG_MODE, &data);
		if (rv == EC_SUCCESS)
			return rv;

		usleep(PS8802_I2C_WAKE_DELAY);
	}

	return rv;
}

int ps8802_detect(int port)
{
	int rv = EC_ERROR_NOT_POWERED;

	/* Detected if we are powered and can read the device */
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF))
		rv = ps8802_i2c_wake(port);

	return rv;
}

static int ps8802_init(int port)
{
	return EC_SUCCESS;
}

static int ps8802_set_mux(int port, mux_state_t mux_state)
{
	int val = (PS8802_MODE_DP_REG_CONTROL
		   | PS8802_MODE_USB_REG_CONTROL
		   | PS8802_MODE_FLIP_REG_CONTROL);
	int rv;

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return (mux_state == TYPEC_MUX_NONE) ? EC_SUCCESS
						     : EC_ERROR_NOT_POWERED;

	rv = ps8802_i2c_wake(port);
	if (rv)
		return rv;

	if (mux_state & MUX_USB_ENABLED)
		val |= PS8802_MODE_USB_ENABLE;
	if (mux_state & MUX_DP_ENABLED)
		val |= PS8802_MODE_DP_ENABLE;
	if (mux_state & MUX_POLARITY_INVERTED)
		val |= PS8802_MODE_FLIP_ENABLE;

	return ps8802_i2c_write(port, PS8802_REG_MODE, val);
}

static int ps8802_get_mux(int port, mux_state_t *mux_state)
{
	int rv;
	int val;

	*mux_state = TYPEC_MUX_NONE;

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	rv = ps8802_i2c_wake(port);
	if (rv)
		return rv;

	rv = ps8802_i2c_read(port, PS8802_REG_MODE, &val);
	if (rv)
		return rv;

	if (val & PS8802_MODE_USB_ENABLE)
		*mux_state |= MUX_USB_ENABLED;
	if (val & PS8802_MODE_DP_ENABLE)
		*mux_state |= MUX_DP_ENABLED;
	if (val & PS8802_MODE_FLIP_ENABLE)
		*mux_state |= MUX_POLARITY_INVERTED;

	return rv;
}

/*
 * PS8802 can look like a retimer or a MUX. So create both tables
 * and use them as needed, until retimers become a type of MUX and
 * then we will only need one of these tables.
 *
 * TODO(b:147593660) Cleanup of retimers as muxes in a more
 * generalized mechanism
 */
const struct usb_retimer_driver ps8802_usb_retimer = {
	.set = ps8802_set_mux,
};
const struct usb_mux_driver ps8802_usb_mux_driver = {
	.init = ps8802_init,
	.set = ps8802_set_mux,
	.get = ps8802_get_mux,
};
