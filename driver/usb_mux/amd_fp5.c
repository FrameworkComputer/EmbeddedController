/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMD FP5 USB/DP Mux.
 */

#include "amd_fp5.h"
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

	if ((mux_state & MUX_USB_ENABLED) && (mux_state & MUX_DP_ENABLED))
		val = (mux_state & MUX_POLARITY_INVERTED)
			? AMD_FP5_MUX_DOCK_INVERTED : AMD_FP5_MUX_DOCK;
	else if (mux_state & MUX_USB_ENABLED)
		val = (mux_state & MUX_POLARITY_INVERTED)
			? AMD_FP5_MUX_USB_INVERTED : AMD_FP5_MUX_USB;
	else if (mux_state & MUX_DP_ENABLED)
		val = (mux_state & MUX_POLARITY_INVERTED)
			? AMD_FP5_MUX_DP_INVERTED : AMD_FP5_MUX_DP;

	return amd_fp5_mux_write(port, val);
}

static int amd_fp5_get_mux(int port, mux_state_t *mux_state)
{
	uint8_t val = 0;
	int rv;

	rv = amd_fp5_mux_read(port, &val);
	if (rv)
		return rv;

	switch (val) {
	case AMD_FP5_MUX_USB:
		*mux_state = MUX_USB_ENABLED;
		break;
	case AMD_FP5_MUX_USB_INVERTED:
		*mux_state = MUX_USB_ENABLED | MUX_POLARITY_INVERTED;
		break;
	case AMD_FP5_MUX_DOCK:
		*mux_state = MUX_USB_ENABLED | MUX_DP_ENABLED;
		break;
	case AMD_FP5_MUX_DOCK_INVERTED:
		*mux_state = MUX_USB_ENABLED | MUX_DP_ENABLED
			     | MUX_POLARITY_INVERTED;
		break;
	case AMD_FP5_MUX_DP:
		*mux_state = MUX_DP_ENABLED;
		break;
	case AMD_FP5_MUX_DP_INVERTED:
		*mux_state = MUX_DP_ENABLED | MUX_POLARITY_INVERTED;
		break;
	default:
		*mux_state = 0;
		break;
	}

	return EC_SUCCESS;
}

const struct usb_mux_driver amd_fp5_usb_mux_driver = {
	.init = amd_fp5_init,
	.set = amd_fp5_set_mux,
	.get = amd_fp5_get_mux,
};
