/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ITE IT5205 Type-C USB alternate mode mux.
 */

#include "common.h"
#include "i2c.h"
#include "it5205.h"
#include "util.h"

#define MUX_STATE_DP_USB_MASK (USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED)

static int it5205_read(const struct usb_mux *me, uint8_t reg, int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static int it5205_write(const struct usb_mux *me, uint8_t reg, uint8_t val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static int it5205h_sbu_update(const struct usb_mux *me, uint8_t reg,
			      uint8_t mask, enum mask_update_action action)
{
	return i2c_update8(me->i2c_port, IT5205H_SBU_I2C_ADDR_FLAGS,
			   reg, mask, action);
}

static int it5205h_sbu_field_update(const struct usb_mux *me, uint8_t reg,
				    uint8_t field_mask, uint8_t set_value)
{
	return i2c_field_update8(me->i2c_port, IT5205H_SBU_I2C_ADDR_FLAGS,
				 reg, field_mask, set_value);
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

static int it5205_init(const struct usb_mux *me)
{
	int i, val, ret;

	/* bit[0]: mux power on, bit[7-1]: reserved. */
	ret = it5205_write(me, IT5205_REG_MUXPDR, 0);
	if (ret)
		return ret;
	/*  Verify chip ID registers. */
	for (i = 0; i < ARRAY_SIZE(mux_chip_id_verify); i++) {
		ret = it5205_read(me, mux_chip_id_verify[i].reg, &val);
		if (ret)
			return ret;

		if (val != mux_chip_id_verify[i].chip_id)
			return EC_ERROR_UNKNOWN;
	}

	if (IS_ENABLED(CONFIG_USB_MUX_IT5205H_SBU_OVP)) {
		RETURN_ERROR(it5205h_sbu_field_update(me, IT5205H_REG_VSR,
				IT5205H_VREF_SELECT_MASK,
				IT5205H_VREF_SELECT_3_3V));

		RETURN_ERROR(it5205h_sbu_field_update(me, IT5205H_REG_CSBUOVPSR,
				IT5205H_OVP_SELECT_MASK,
				IT5205H_OVP_3_68V));

		RETURN_ERROR(it5205h_sbu_update(me, IT5205H_REG_ISR,
				IT5205H_ISR_CSBU_MASK, MASK_CLR));

		RETURN_ERROR(it5205h_enable_csbu_switch(me, true));
	}

	return EC_SUCCESS;
}

enum ec_error_list it5205h_enable_csbu_switch(const struct usb_mux *me, bool en)
{
	return it5205h_sbu_update(me, IT5205H_REG_CSBUSR,
			IT5205H_CSBUSR_SWITCH, en ? MASK_SET : MASK_CLR);
}

/* Writes control register to set switch mode */
static int it5205_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	uint8_t reg;

	switch (mux_state & MUX_STATE_DP_USB_MASK) {
	case USB_PD_MUX_USB_ENABLED:
		reg = IT5205_USB;
		break;
	case USB_PD_MUX_DP_ENABLED:
		reg = IT5205_DP;
		break;
	case MUX_STATE_DP_USB_MASK:
		reg = IT5205_DP_USB;
		break;
	default:
		reg = 0;
		break;
	}

	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= IT5205_POLARITY_INVERTED;

	return it5205_write(me, IT5205_REG_MUXCR, reg);
}

/* Reads control register and updates mux_state accordingly */
static int it5205_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int reg, ret;

	ret = it5205_read(me, IT5205_REG_MUXCR, &reg);
	if (ret)
		return ret;

	switch (reg & IT5205_DP_USB_CTRL_MASK) {
	case IT5205_USB:
		*mux_state = USB_PD_MUX_USB_ENABLED;
		break;
	case IT5205_DP:
		*mux_state = USB_PD_MUX_DP_ENABLED;
		break;
	case IT5205_DP_USB:
		*mux_state = MUX_STATE_DP_USB_MASK;
		break;
	default:
		*mux_state = 0;
		break;
	}

	if (reg & IT5205_POLARITY_INVERTED)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

static int it5205_enter_low_power_mode(const struct usb_mux *me)
{
	int rv;

	/* Turn off all switches */
	rv = it5205_write(me, IT5205_REG_MUXCR, 0);

	if (rv)
		return rv;

	/* Power down mux */
	return it5205_write(me, IT5205_REG_MUXPDR, IT5205_MUX_POWER_DOWN);
}

const struct usb_mux_driver it5205_usb_mux_driver = {
	.init = &it5205_init,
	.set = &it5205_set_mux,
	.get = &it5205_get_mux,
	.enter_low_power_mode = &it5205_enter_low_power_mode,
};
