/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Analogix ANX7440 USB Type-C Active mux with
 * Integrated Re-timers for USB3.1/DisplayPort.
 */

#include "anx7440.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "usb_mux.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static inline int anx7440_read(int port, uint8_t reg, int *val)
{
	return i2c_read8(I2C_PORT_USB_MUX, MUX_ADDR(port), reg, val);
}

static inline int anx7440_write(int port, uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_USB_MUX, MUX_ADDR(port), reg, val);
}

struct anx7440_id_t {
	uint8_t val;
	uint8_t reg;
};

static const struct anx7440_id_t anx7440_device_ids[] = {
	{ ANX7440_VENDOR_ID_L, ANX7440_REG_VENDOR_ID_L },
	{ ANX7440_VENDOR_ID_H, ANX7440_REG_VENDOR_ID_H },
	{ ANX7440_DEVICE_ID_L, ANX7440_REG_DEVICE_ID_L },
	{ ANX7440_DEVICE_ID_H, ANX7440_REG_DEVICE_ID_H },
	{ ANX7440_DEVICE_VERSION, ANX7440_REG_DEVICE_VERSION },
};

static int anx7440_init(int port)
{
	int i;
	int val;
	int res;

	/* Verify device id / version registers */
	for (i = 0; i < ARRAY_SIZE(anx7440_device_ids); i++) {
		res = anx7440_read(port, anx7440_device_ids[i].reg, &val);
		if (res)
			return res;

		if (val != anx7440_device_ids[i].val)
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/* Writes control register to set switch mode */
static int anx7440_set_mux(int port, mux_state_t mux_state)
{
	int reg, res;

	res = anx7440_read(port, ANX7440_REG_CHIP_CTRL, &reg);
	if (res)
		return res;

	reg &= ~ANX7440_CHIP_CTRL_SW_OP_MODE_CLEAR;
	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= ANX7440_CHIP_CTRL_SW_OP_MODE_USB;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= ANX7440_CHIP_CTRL_SW_OP_MODE_DP;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= ANX7440_CHIP_CTRL_SW_FLIP;

	return anx7440_write(port, ANX7440_REG_CHIP_CTRL, reg);
}

/* Reads control register and updates mux_state accordingly */
static int anx7440_get_mux(int port, mux_state_t *mux_state)
{
	int reg, res;

	*mux_state = 0;
	res = anx7440_read(port, ANX7440_REG_CHIP_CTRL, &reg);
	if (res)
		return res;

	if (reg & ANX7440_CHIP_CTRL_OP_MODE_FINAL_USB)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (reg & ANX7440_CHIP_CTRL_OP_MODE_FINAL_DP)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (reg & ANX7440_CHIP_CTRL_FINAL_FLIP)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

const struct usb_mux_driver anx7440_usb_mux_driver = {
	.init = anx7440_init,
	.set = anx7440_set_mux,
	.get = anx7440_get_mux,
	/* TODO(b/146683781): add low power mode */
};
