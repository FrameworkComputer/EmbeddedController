/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8822
 * USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#include "common.h"
#include "i2c.h"
#include "ps8822.h"
#include "usb_mux.h"
#include "util.h"

static int ps8822_read(const struct usb_mux *me, int page, uint8_t reg,
		       int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags + page,
			 reg, val);
}

static int ps8822_write(const struct usb_mux *me, int page, uint8_t reg,
			int val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags + page,
			reg, val);
}

int ps8822_set_dp_rx_eq(const struct usb_mux *me, int db)
{
	int dpeq_reg;
	int rv;

	/* Read DP EQ register */
	rv = ps8822_read(me, PS8822_REG_PAGE1, PS8822_REG_DP_EQ,
			 &dpeq_reg);
	if (rv)
		return rv;

	if (db < PS8822_DPEQ_LEVEL_UP_9DB || db > PS8822_DPEQ_LEVEL_UP_21DB)
		return EC_ERROR_INVAL;

	/* Disable auto eq */
	dpeq_reg &= ~PS8822_DP_EQ_AUTO_EN;

	/* Set gain to the requested value */
	dpeq_reg &= ~(PS8822_DPEQ_LEVEL_UP_MASK <<
		      PS8822_REG_DP_EQ_SHIFT);
	dpeq_reg |= (db << PS8822_REG_DP_EQ_SHIFT);

	/* Apply new EQ setting */
	return ps8822_write(me, PS8822_REG_PAGE1, PS8822_REG_DP_EQ,
			  dpeq_reg);
}

static int ps8822_init(const struct usb_mux *me)
{
	char id[PS8822_ID_LEN + 1];
	int reg;
	int i;
	int rv = 0;

	/* Read ID registers */
	for (i = 0; i < PS8822_ID_LEN; i++) {
		rv |= ps8822_read(me, PS8822_REG_PAGE0, PS8822_REG_DEV_ID1 + i,
				  &reg);
		if (!rv)
			id[i] = reg;
	}

	if (!rv) {
		id[PS8822_ID_LEN] = '\0';
		/* Set mode register to default value */
		rv = ps8822_write(me, PS8822_REG_PAGE0, PS8822_REG_MODE, 0);
		rv |= strcasecmp("PS8822", id);
	}

	return rv;
}

/* Writes control register to set switch mode */
static int ps8822_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			  bool *ack_required)
{
	int reg;
	int rv;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	rv = ps8822_read(me, PS8822_REG_PAGE0, PS8822_REG_MODE, &reg);
	if (rv)
		return rv;

	/* Assume standby, preserve PIN_E config bit */
	reg &= ~(PS8822_MODE_ALT_DP_EN | PS8822_MODE_USB_EN | PS8822_MODE_FLIP);

	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= PS8822_MODE_USB_EN;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= PS8822_MODE_ALT_DP_EN;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= PS8822_MODE_FLIP;

	return ps8822_write(me, PS8822_REG_PAGE0, PS8822_REG_MODE, reg);
}

/* Reads control register and updates mux_state accordingly */
static int ps8822_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int reg;
	int res;

	res = ps8822_read(me, PS8822_REG_PAGE0, PS8822_REG_MODE, &reg);
	if (res)
		return res;

	*mux_state = 0;
	if (reg & PS8822_MODE_USB_EN)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (reg & PS8822_MODE_ALT_DP_EN)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (reg & PS8822_MODE_FLIP)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}


const struct usb_mux_driver ps8822_usb_mux_driver = {
	.init = ps8822_init,
	.set = ps8822_set_mux,
	.get = ps8822_get_mux,
};
