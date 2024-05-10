/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver for Kandou KB800x USB-C 40 Gb/s multiprotocol switch.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "kb800x.h"
#include "time.h"

/* Time between load switch enable and the reset being de-asserted */
#define KB800X_POWER_ON_DELAY_MS 20
#define KB800X_REG_OFFSET_MAX UINT16_MAX
#define KB800X_REG_DATA_MAX UINT8_MAX

static mux_state_t cached_mux_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static int kb800x_write(const struct usb_mux *me, uint32_t address,
			uint32_t data)
{
	uint8_t kb800x_config[3] = { 0x00, 0x00, 0x00 };

	/* Validate the register address */
	if (address > KB800X_REG_OFFSET_MAX)
		return EC_ERROR_INVAL;

	/* Validate the writeable data */
	if (data > KB800X_REG_DATA_MAX)
		return EC_ERROR_INVAL;

	kb800x_config[0] = (address >> 8) & 0xff;
	kb800x_config[1] = address & 0xff;
	kb800x_config[2] = (uint8_t)data;
	return i2c_xfer(me->i2c_port, me->i2c_addr_flags, kb800x_config,
			sizeof(kb800x_config), NULL, 0);
}

static int kb800x_read(const struct usb_mux *me, uint32_t address,
		       uint32_t *data)
{
	uint8_t kb800x_config[2] = { 0x00, 0x00 };

	/* Validate the register address */
	if (address > KB800X_REG_OFFSET_MAX)
		return EC_ERROR_INVAL;

	kb800x_config[0] = (address >> 8) & 0xff;
	kb800x_config[1] = address & 0xff;
	return i2c_xfer(me->i2c_port, me->i2c_addr_flags, kb800x_config,
			sizeof(kb800x_config), (uint8_t *)data, 1);
}

#ifdef CONFIG_KB800X_CUSTOM_XBAR

/* These lookup tables are derived from the KB8001 EVB GUI register map */

/* Map elastic buffer (EB) to register field for TX configuration. */
static const uint8_t tx_eb_to_field_ab[] = {
	[KB800X_EB1] = 4, [KB800X_EB2] = 0, [KB800X_EB3] = 0,
	[KB800X_EB4] = 1, [KB800X_EB5] = 2, [KB800X_EB6] = 3
};
static const uint8_t tx_eb_to_field_cd[] = {
	[KB800X_EB1] = 1, [KB800X_EB2] = 2, [KB800X_EB3] = 3,
	[KB800X_EB4] = 4, [KB800X_EB5] = 0, [KB800X_EB6] = 0
};
/* Map phy lane to register field for RX configuration */
static const uint8_t rx_phy_lane_to_field[] = {
	[KB800X_A0] = 1, [KB800X_A1] = 2, [KB800X_B0] = 5, [KB800X_B1] = 6,
	[KB800X_C0] = 1, [KB800X_C1] = 2, [KB800X_D0] = 5, [KB800X_D1] = 6
};
/* Map EB to address for RX configuration */
static const uint16_t rx_eb_to_address[] = {
	[KB800X_EB1] = KB800X_REG_XBAR_EB1SEL,
	[KB800X_EB2] = KB800X_REG_XBAR_EB23SEL,
	[KB800X_EB3] = KB800X_REG_XBAR_EB23SEL,
	[KB800X_EB4] = KB800X_REG_XBAR_EB4SEL,
	[KB800X_EB5] = KB800X_REG_XBAR_EB56SEL,
	[KB800X_EB6] = KB800X_REG_XBAR_EB56SEL
};
/* Map SS lane to EB for DP or USB/CIO protocols */
static const uint8_t dp_ss_lane_to_eb[] = { [KB800X_TX0] = KB800X_EB4,
					    [KB800X_TX1] = KB800X_EB5,
					    [KB800X_RX0] = KB800X_EB6,
					    [KB800X_RX1] = KB800X_EB1 };
static const uint8_t usb_ss_lane_to_eb[] = { [KB800X_TX0] = KB800X_EB4,
					     [KB800X_TX1] = KB800X_EB5,
					     [KB800X_RX0] = KB800X_EB1,
					     [KB800X_RX1] = KB800X_EB2 };

/* Assign a phy TX to an elastic buffer */
static int kb800x_assign_tx_to_eb(const struct usb_mux *me,
				  enum kb800x_phy_lane phy_lane,
				  enum kb800x_eb eb)
{
	uint8_t field_value = 0;
	uint32_t regval;
	int rv;

	field_value = KB800X_PHY_IS_AB(phy_lane) ? tx_eb_to_field_ab[eb] :
						   tx_eb_to_field_cd[eb];

	/* For lane1 of each PHY, shift by 3 bits */
	field_value <<= 3 * KB800X_LANE_NUMBER_FROM_PHY(phy_lane);

	rv = kb800x_read(me, KB800X_REG_TXSEL_FROM_PHY(phy_lane), &regval);
	if (rv)
		return rv;
	return kb800x_write(me, KB800X_REG_TXSEL_FROM_PHY(phy_lane),
			    regval | field_value);
}

/* Assign a phy RX to an elastic buffer */
static int kb800x_assign_rx_to_eb(const struct usb_mux *me,
				  enum kb800x_phy_lane phy_lane,
				  enum kb800x_eb eb)
{
	uint16_t address = 0;
	uint8_t field_value = 0;
	uint32_t regval = 0;
	int rv;

	field_value = rx_phy_lane_to_field[phy_lane];
	address = rx_eb_to_address[eb];

	/*
	 * need to shift by 4 for reverse EB or 3rd EB in set based on the
	 * register definition from the KB8001 EVB register map
	 */
	switch (eb) {
	case KB800X_EB1:
		if (!KB800X_PHY_IS_AB(phy_lane))
			field_value <<= 4;
		break;
	case KB800X_EB4:
		if (KB800X_PHY_IS_AB(phy_lane))
			field_value <<= 4;
		break;
	case KB800X_EB3:
	case KB800X_EB6:
		field_value <<= 4;
		break;
	default:
		break;
	}

	rv = kb800x_read(me, address, &regval);
	if (rv)
		return rv;
	return kb800x_write(me, address, regval | field_value);
}

static bool kb800x_in_dpmf(const struct usb_mux *me)
{
	if ((cached_mux_state[me->usb_port] & USB_PD_MUX_DP_ENABLED) &&
	    (cached_mux_state[me->usb_port] & USB_PD_MUX_USB_ENABLED))
		return true;
	else
		return false;
}

static bool kb800x_is_dp_lane(const struct usb_mux *me,
			      enum kb800x_ss_lane ss_lane)
{
	if (cached_mux_state[me->usb_port] & USB_PD_MUX_DP_ENABLED) {
		/* DP ALT mode */
		if (kb800x_in_dpmf(me)) {
			/* DPMF pin configuration */
			if ((ss_lane == KB800X_TX1) ||
			    (ss_lane == KB800X_RX1)) {
				return true; /* ML0 or ML1 */
			}
		} else {
			/* Pure, 4-lane DP mode */
			return true;
		}
	}
	/* Not a DP mode or ML2/3 while in DPMF */
	return false;
}

/* Assigning this PHY to this SS lane means it should be RX */
static bool kb800x_phy_ss_lane_is_rx(enum kb800x_phy_lane phy_lane,
				     enum kb800x_ss_lane ss_lane)
{
	bool rx;

	switch (ss_lane) {
	case KB800X_TX0:
	case KB800X_TX1:
		rx = false;
		break;
	case KB800X_RX0:
	case KB800X_RX1:
		rx = true;
		break;
	}
	/* invert for C/D (host side), since it is receiving the TX signal*/
	if (!KB800X_PHY_IS_AB(phy_lane))
		return !rx;
	return rx;
}

/* Assign SS lane to PHY. Assumes A/B is connector-side, and C/D is host-side */
static int kb800x_assign_lane(const struct usb_mux *me,
			      enum kb800x_phy_lane phy_lane,
			      enum kb800x_ss_lane ss_lane)
{
	enum kb800x_eb eb = 0;

	/*
	 * Easiest way to handle flipping is to just swap lane 1/0. This assumes
	 * lanes are flipped in the AP. If they are not, they shouldn't be
	 * flipped for the AP-side lanes, but should for connector-side
	 */
	if (cached_mux_state[me->usb_port] & USB_PD_MUX_POLARITY_INVERTED)
		ss_lane = KB800X_FLIP_SS_LANE(ss_lane);

	if (kb800x_is_dp_lane(me, ss_lane)) {
		if (kb800x_in_dpmf(me)) {
			/* Route USB3 RX/TX to EB1/4, and ML0/1 to EB5/6 */
			switch (ss_lane) {
			case KB800X_TX1: /* ML1 */
				eb = KB800X_EB6;
				break;
			case KB800X_RX1: /* ML0 */
				eb = KB800X_EB5;
				break;
			default:
				break;
			}
		} else {
			/* Route ML0/1/2/3 through EB1/5/4/6 */
			eb = dp_ss_lane_to_eb[ss_lane];
		}

		/* For DP lanes, always DFP so A/B is TX, C/D is RX */
		if (KB800X_PHY_IS_AB(phy_lane))
			return kb800x_assign_tx_to_eb(me, phy_lane, eb);
		else
			return kb800x_assign_rx_to_eb(me, phy_lane, eb);
	}

	/* Lane is either USB3 or CIO */
	if (kb800x_phy_ss_lane_is_rx(phy_lane, ss_lane))
		return kb800x_assign_rx_to_eb(me, phy_lane,
					      usb_ss_lane_to_eb[ss_lane]);
	else
		return kb800x_assign_tx_to_eb(me, phy_lane,
					      usb_ss_lane_to_eb[ss_lane]);
}

static int kb800x_xbar_override(const struct usb_mux *me)
{
	int rv;
	int i;

	for (i = KB800X_A0; i < KB800X_PHY_LANE_COUNT; ++i) {
		rv = kb800x_assign_lane(
			me, i, kb800x_control[me->usb_port].ss_lanes[i]);
		if (rv)
			return rv;
	}
	return kb800x_write(me, KB800X_REG_XBAR_OVR, KB800X_XBAR_OVR_EN);
}
#endif /* CONFIG_KB800X_CUSTOM_XBAR */

/*
 * The initialization writes for each protocol can be found in the KB8001/KB8002
 * Programming Guidelines
 */
static const uint16_t global_init_addresses[] = {
	0x5058, 0x5059, 0xFF63, 0xF021, 0xF022, 0xF057, 0xF058,
	0x8194, 0xF0C9, 0xF0CA, 0xF0CB, 0xF0CC, 0xF0CD, 0xF0CE,
	0xF0DF, 0xF0E0, 0xF0E1, 0x8198, 0x8191
};
static const uint8_t global_init_values[] = { 0x12, 0x12, 0x3C, 0x02, 0x02,
					      0x02, 0x02, 0x37, 0x0C, 0x0B,
					      0x0A, 0x09, 0x08, 0x07, 0x57,
					      0x66, 0x66, 0x33, 0x00 };
static const uint16_t usb3_init_addresses[] = { 0xF020, 0xF056 };
static const uint8_t usb3_init_values[] = { 0x2f, 0x2f };
static const uint16_t dp_init_addresses[] = { 0xF2CB, 0x0011 };
static const uint8_t dp_init_values[] = { 0x30, 0x00 };
/*
 * The first 2 CIO writes apply an SBRX pullup to the host side (C/D)
 * This is required when the CPU doesn't apply a pullup.
 */
static const uint16_t cio_init_addresses[] = { 0x81fd, 0x81fe, 0xF26B, 0xF26E };
static const uint8_t cio_init_values[] = { 0x08, 0x80, 0x01, 0x19 };

static int kb800x_bulk_write(const struct usb_mux *me,
			     const uint16_t *addresses, const uint8_t *values,
			     const uint8_t size)
{
	int i;
	int rv;

	for (i = 0; i < size; ++i) {
		rv = kb800x_write(me, addresses[i], values[i]);
		if (rv != EC_SUCCESS)
			return rv;
	}

	return EC_SUCCESS;
}

static int kb800x_global_init(const struct usb_mux *me)
{
	return kb800x_bulk_write(me, global_init_addresses, global_init_values,
				 sizeof(global_init_values));
}

static int kb800x_dp_init(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv;

	rv = kb800x_bulk_write(me, dp_init_addresses, dp_init_values,
			       sizeof(dp_init_values));
	if (rv)
		return rv;
	return kb800x_write(
		me, KB800X_REG_ORIENTATION,
		KB800X_ORIENTATION_DP_DFP |
			((mux_state & USB_PD_MUX_POLARITY_INVERTED) ?
				 KB800X_ORIENTATION_POLARITY :
				 0x0));
}

static int kb800x_usb3_init(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv;

	rv = kb800x_bulk_write(me, usb3_init_addresses, usb3_init_values,
			       sizeof(usb3_init_values));
	if (rv)
		return rv;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		/* This will be overwritten in the DPMF case */
		return kb800x_write(me, KB800X_REG_ORIENTATION,
				    KB800X_ORIENTATION_POLARITY);
	return EC_SUCCESS;
}

static int kb800x_cio_init(const struct usb_mux *me, mux_state_t mux_state)
{
	uint8_t orientation = 0x0;
	int rv;

	enum idh_ptype cable_type = get_usb_pd_cable_type(me->usb_port);
	union tbt_mode_resp_cable cable_resp = {
		.raw_value =
			pd_get_tbt_mode_vdo(me->usb_port, TCPCI_MSG_SOP_PRIME)
	};

	rv = kb800x_bulk_write(me, cio_init_addresses, cio_init_values,
			       sizeof(cio_init_values));
	if (rv)
		return rv;

	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		orientation = KB800X_ORIENTATION_CIO_LANE_SWAP |
			      KB800X_ORIENTATION_POLARITY;

	if (!(mux_state & USB_PD_MUX_USB4_ENABLED)) {
		/* Special configuration only for legacy mode */
		if (cable_type == IDH_PTYPE_ACABLE ||
		    cable_resp.tbt_active_passive == TBT_CABLE_ACTIVE) {
			/* Active cable */
			if (cable_resp.lsrx_comm == UNIDIR_LSRX_COMM) {
				orientation |=
					KB800X_ORIENTATION_CIO_LEGACY_UNIDIR;
			} else {
				/* 'Pre-Coding on a TBT3-Compatible Link' ECN */
				rv = kb800x_write(me, 0x8194, 0x31);
				if (rv)
					return rv;
				orientation |=
					KB800X_ORIENTATION_CIO_LEGACY_BIDIR;
			}
		} else {
			/* Passive Cable */
			orientation |= KB800X_ORIENTATION_CIO_LEGACY_PASSIVE;
		}
	}
	return kb800x_write(me, KB800X_REG_ORIENTATION, orientation);
}

static int kb800x_set_state(const struct usb_mux *me, mux_state_t mux_state,
			    bool *ack_required)
{
	int rv;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	cached_mux_state[me->usb_port] = mux_state;
	rv = kb800x_write(me, KB800X_REG_RESET, KB800X_RESET_MASK);
	if (rv)
		return rv;
	/* Release memory map reset */
	rv = kb800x_write(me, KB800X_REG_RESET,
			  KB800X_RESET_MASK & ~KB800X_RESET_MM);
	if (rv)
		return rv;

	/* Already in reset, nothing to do */
	if ((mux_state == USB_PD_MUX_NONE) ||
	    (mux_state & USB_PD_MUX_SAFE_MODE))
		return EC_SUCCESS;

	rv = kb800x_global_init(me);
	if (rv)
		return rv;

	/* CIO mode (USB4/TBT) */
	if (mux_state &
	    (USB_PD_MUX_USB4_ENABLED | USB_PD_MUX_TBT_COMPAT_ENABLED)) {
		rv = kb800x_cio_init(me, mux_state);
		if (rv)
			return rv;
		rv = kb800x_write(me, KB800X_REG_PROTOCOL, KB800X_PROTOCOL_CIO);
	} else {
		/* USB3 enabled (USB3-only or DPMF) */
		if (mux_state & USB_PD_MUX_USB_ENABLED) {
			rv = kb800x_usb3_init(me, mux_state);
			if (rv)
				return rv;
			/* USB3-only is the default KB800X_REG_PROTOCOL value */
		}

		/* DP alt modes (DP-only or DPMF) */
		if (mux_state & USB_PD_MUX_DP_ENABLED) {
			rv = kb800x_dp_init(me, mux_state);
			if (rv)
				return rv;
			if (mux_state & USB_PD_MUX_USB_ENABLED)
				rv = kb800x_write(me, KB800X_REG_PROTOCOL,
						  KB800X_PROTOCOL_DPMF);
			else
				rv = kb800x_write(me, KB800X_REG_PROTOCOL,
						  KB800X_PROTOCOL_DP);
		}
	}
	if (rv)
		return rv;

#ifdef CONFIG_KB800X_CUSTOM_XBAR
	rv = kb800x_xbar_override(me);
	if (rv)
		return rv;
#endif /* CONFIG_KB800X_CUSTOM_XBAR */

	return kb800x_write(me, KB800X_REG_RESET, 0x00);
}

static int kb800x_init(const struct usb_mux *me)
{
	bool unused;

	gpio_set_level(kb800x_control[me->usb_port].usb_ls_en_gpio, 1);
	gpio_set_level(kb800x_control[me->usb_port].retimer_rst_gpio, 1);

	/*
	 * Delay after enabling power and releasing the reset to allow the power
	 * to come up and the reset to be released by the power sequencing
	 * logic. If after the delay, the reset is still held low - return an
	 * error.
	 */
	crec_msleep(KB800X_POWER_ON_DELAY_MS);
	if (!gpio_get_level(kb800x_control[me->usb_port].retimer_rst_gpio))
		return EC_ERROR_NOT_POWERED;

	return kb800x_set_state(me, USB_PD_MUX_NONE, &unused);
}

static int kb800x_enter_low_power_mode(const struct usb_mux *me)
{
	gpio_set_level(kb800x_control[me->usb_port].retimer_rst_gpio, 0);
	/* Power-down sequencing must be handled in HW */
	gpio_set_level(kb800x_control[me->usb_port].usb_ls_en_gpio, 0);

	return EC_SUCCESS;
}

const struct usb_mux_driver kb800x_usb_mux_driver = {
	.init = kb800x_init,
	.set = kb800x_set_state,
	.enter_low_power_mode = kb800x_enter_low_power_mode,
#ifdef CONFIG_CMD_RETIMER
	.retimer_read = kb800x_read,
	.retimer_write = kb800x_write,
#endif /* CONFIG_CMD_RETIMER */
};
