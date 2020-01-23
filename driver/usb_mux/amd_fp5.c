/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMD FP5 USB/DP Mux.
 */

#include "amd_fp5.h"
#include "chipset.h"
#include "common.h"
#include "i2c.h"
#include "usb_mux.h"

static inline int amd_fp5_mux_read(int port, uint8_t *val)
{
	uint8_t buf[3] = { 0 };
	int rv;

	rv = i2c_xfer(I2C_PORT_USB_MUX, AMD_FP5_MUX_I2C_ADDR_FLAGS,
		      NULL, 0, buf, 3);
	if (rv)
		return rv;

	*val = buf[port + 1];

	return EC_SUCCESS;
}

static inline int amd_fp5_mux_write(int port, uint8_t val)
{
	return i2c_write8(I2C_PORT_USB_MUX, AMD_FP5_MUX_I2C_ADDR_FLAGS,
			  port, val);
}

static int amd_fp5_init(int port)
{
	return EC_SUCCESS;
}

static int amd_fp5_set_mux(int port, mux_state_t mux_state)
{
	uint8_t val = 0;

	/*
	 * This MUX is on the FP5 SoC.  If that device is not powered then
	 * we either have to complain that it is not powered or if we were
	 * setting the state to OFF, then go ahead and report that we did
	 * it because a powered down MUX is off.
	 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return (mux_state == USB_PD_MUX_NONE)
			? EC_SUCCESS
			: EC_ERROR_NOT_POWERED;

	if ((mux_state & USB_PD_MUX_USB_ENABLED) &&
		(mux_state & USB_PD_MUX_DP_ENABLED))
		val = (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			? AMD_FP5_MUX_DOCK_INVERTED : AMD_FP5_MUX_DOCK;
	else if (mux_state & USB_PD_MUX_USB_ENABLED)
		val = (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			? AMD_FP5_MUX_USB_INVERTED : AMD_FP5_MUX_USB;
	else if (mux_state & USB_PD_MUX_DP_ENABLED)
		val = (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			? AMD_FP5_MUX_DP_INVERTED : AMD_FP5_MUX_DP;

	return amd_fp5_mux_write(port, val);
}

static int amd_fp5_get_mux(int port, mux_state_t *mux_state)
{
	uint8_t val = AMD_FP5_MUX_SAFE;

	/*
	 * This MUX is on the FP5 SoC.  Only access the device if we
	 * have power.  If that device is not powered then claim the
	 * state to be NONE, which is SAFE.
	 */
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		int rv;

		rv = amd_fp5_mux_read(port, &val);
		if (rv)
			return rv;
	}


	switch (val) {
	case AMD_FP5_MUX_USB:
		*mux_state = USB_PD_MUX_USB_ENABLED;
		break;
	case AMD_FP5_MUX_USB_INVERTED:
		*mux_state = USB_PD_MUX_USB_ENABLED |
				USB_PD_MUX_POLARITY_INVERTED;
		break;
	case AMD_FP5_MUX_DOCK:
		*mux_state = USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED;
		break;
	case AMD_FP5_MUX_DOCK_INVERTED:
		*mux_state = USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED
			     | USB_PD_MUX_POLARITY_INVERTED;
		break;
	case AMD_FP5_MUX_DP:
		*mux_state = USB_PD_MUX_DP_ENABLED;
		break;
	case AMD_FP5_MUX_DP_INVERTED:
		*mux_state = USB_PD_MUX_DP_ENABLED |
				USB_PD_MUX_POLARITY_INVERTED;
		break;
	case AMD_FP5_MUX_SAFE:
	default:
		*mux_state = USB_PD_MUX_NONE;
		break;
	}

	return EC_SUCCESS;
}

/*
 * The FP5 MUX can look like a retimer or a MUX. So create both tables
 * and use them as needed, until retimers become a type of MUX and
 * then we will only need one of these tables.
 *
 * TODO(b:147593660) Cleanup of retimers as muxes in a more
 * generalized mechanism
 */
const struct usb_retimer_driver amd_fp5_usb_retimer = {
	.set = amd_fp5_set_mux,
};
const struct usb_mux_driver amd_fp5_usb_mux_driver = {
	.init = amd_fp5_init,
	.set = amd_fp5_set_mux,
	.get = amd_fp5_get_mux,
};
