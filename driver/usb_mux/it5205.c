/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ITE IT5205 Type-C USB alternate mode mux.
 */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "it5205.h"
#include "usb_mux.h"
#include "util.h"

#define MUX_STATE_DP_USB_MASK (MUX_USB_ENABLED | MUX_DP_ENABLED)

static int it5205_read(int port, uint8_t reg, int *val)
{
	return i2c_read8(I2C_PORT_USB_MUX, MUX_ADDR(port), reg, val);
}

static int it5205_write(int port, uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_USB_MUX, MUX_ADDR(port), reg, val);
}

struct mux_chip_id_t {
	uint8_t chip_id;
	uint8_t reg;
};

static const struct mux_chip_id_t mux_chip_id_verify[] = {
	{ '5', IT5205_REG_CHIP_ID3},
	{ '2', IT5205_REG_CHIP_ID2},
	{ '0', IT5205_REG_CHIP_ID1},
	{ '5', IT5205_REG_CHIP_ID0},
};

static int it5205_init(int port)
{
	int i, val, ret;

	/* bit[0]: mux power on, bit[7-1]: reserved. */
	ret = it5205_write(port, IT5205_REG_MUXPDR, 0);
	if (ret)
		return ret;
	/*  Verify chip ID registers. */
	for (i = 0; i < ARRAY_SIZE(mux_chip_id_verify); i++) {
		ret = it5205_read(port, mux_chip_id_verify[i].reg, &val);
		if (ret)
			return ret;

		if (val != mux_chip_id_verify[i].chip_id)
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/* Writes control register to set switch mode */
static int it5205_set_mux(int port, mux_state_t mux_state)
{
	uint8_t reg;

	switch (mux_state & MUX_STATE_DP_USB_MASK) {
	case MUX_USB_ENABLED:
		reg = IT5205_USB;
		break;
	case MUX_DP_ENABLED:
		reg = IT5205_DP;
		break;
	case MUX_STATE_DP_USB_MASK:
		reg = IT5205_DP_USB;
		break;
	default:
		reg = 0;
		break;
	}

	if (mux_state & MUX_POLARITY_INVERTED)
		reg |= IT5205_POLARITY_INVERTED;

	return it5205_write(port, IT5205_REG_MUXCR, reg);
}

/* Reads control register and updates mux_state accordingly */
static int it5205_get_mux(int port, mux_state_t *mux_state)
{
	int reg, ret;

	ret = it5205_read(port, IT5205_REG_MUXCR, &reg);
	if (ret)
		return ret;

	switch (reg & IT5205_DP_USB_CTRL_MASK) {
	case IT5205_USB:
		*mux_state = MUX_USB_ENABLED;
		break;
	case IT5205_DP:
		*mux_state = MUX_DP_ENABLED;
		break;
	case IT5205_DP_USB:
		*mux_state = MUX_STATE_DP_USB_MASK;
		break;
	default:
		*mux_state = 0;
		break;
	}

	if (reg & IT5205_POLARITY_INVERTED)
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

static int it5205_enter_low_power_mode(int port)
{
	int rv;

	/* Turn off all switches */
	rv = it5205_write(port, IT5205_REG_MUXCR, 0);

	if (rv)
		return rv;

	/* Power down mux */
	return it5205_write(port, IT5205_REG_MUXPDR, IT5205_MUX_POWER_DOWN);
}

const struct usb_mux_driver it5205_usb_mux_driver = {
	.init = &it5205_init,
	.set = &it5205_set_mux,
	.get = &it5205_get_mux,
	.enter_low_power_mode = &it5205_enter_low_power_mode,
};
