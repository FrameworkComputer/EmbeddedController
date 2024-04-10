/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver for Kandou KB801x USB-C 40 Gb/s multiprotocol switch.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "kb8010.h"
#include "retimer/kb8010_public.h"
#include "timer.h"

/* Time between load switch enable and the reset being de-asserted */
#define KB8010_POWER_ON_DELAY_MS 20

#define KB8010_LANE_CFG_LEN 8
enum kb8010_modes {
	KB8010_USB3 = KB8010_PROTOCOL_USB3,
	KB8010_DPMF = KB8010_PROTOCOL_DPMF,
	KB8010_DP = KB8010_PROTOCOL_DP,
	KB8010_USB4_TBT = KB8010_PROTOCOL_USB4,
	KB8010_NUM_MODES
};

struct kb80010_reg_desc {
	uint16_t offset;
	uint8_t val;
};

const static struct kb80010_reg_desc kb8010_dp_cfg[] = {
	{ KB8010_REG_ORIENTATION, 0x06 },
	{ KB8010_REG_SBBR_COMTX_OUTPUT_DRIVER_MISC_OVR_EN, 0x02 },
	{ KB8010_REG_DP_L_EQ_CFG, 0x09 },
	{ KB8010_REG_DFP_REPLY_TIMEOUT, 0x60 },
	{ 0xFF62, 0x0A },
	{ 0xFFFC, 0x49 },
	{ 0xF37F, 0x1E },
	{ 0xFEE9, 0x06 },
	{ KB8010_REG_DP_D_IEEE_OUI, 0xba },
	{ KB8010_REG_DP_D_FUNC_1, 0x67 },
	{ KB8010_REG_DP_D_FUNC_2, 0x91 },
};

const static uint8_t kb8010_flip_cfg[KB8010_NUM_MODES][KB8010_LANE_CFG_LEN] = {
	{ 0x05, 0x02, 0x05, 0x02, 0x02, 0x08, 0x02, 0x08 }, /* USB3 */
	{ 0x05, 0x20, 0x05, 0x01, 0x13, 0x08, 0x00, 0x08 }, /* DPMF */
	{ 0x50, 0x20, 0x06, 0x01, 0x13, 0x21, 0x00, 0x00 }, /* DP */
	{ 0x05, 0x02, 0x05, 0x02, 0x02, 0x08, 0x02, 0x08 }, /* USB4 */
};

static int kb8010_write(const struct usb_mux *me, uint16_t address,
			uint8_t data)
{
	uint8_t kb8010_config[3];

	kb8010_config[0] = (address >> 8) & 0xff;
	kb8010_config[1] = address & 0xff;
	kb8010_config[2] = data;
	return i2c_xfer(me->i2c_port, me->i2c_addr_flags, kb8010_config,
			sizeof(kb8010_config), NULL, 0);
}

static int kb8010_pair_write(const struct usb_mux *me,
			     const struct kb80010_reg_desc *pair,
			     const int num_pairs)
{
	int i;
	int rv;

	for (i = 0; i < num_pairs; ++i) {
		rv = kb8010_write(me, pair[i].offset, pair[i].val);
		if (rv != EC_SUCCESS) {
			return rv;
		}
	}

	return EC_SUCCESS;
}

static int kb8010_sequential_write(const struct usb_mux *me,
				   const uint16_t start_addr,
				   const uint8_t *values, const uint8_t size)
{
	int i;
	int rv;

	for (i = 0; i < size; ++i) {
		rv = kb8010_write(me, start_addr + i, values[i]);
		if (rv != EC_SUCCESS) {
			return rv;
		}
	}

	return EC_SUCCESS;
}

static int kb8010_common_init(const struct usb_mux *me)
{
	static const struct kb80010_reg_desc kb8010_init_cfg[] = {
		{ KB8010_REG_SBBR_COMRX_CH_SHARED_LINK_CTRL_RUN_POST_CDR_OFFSET,
		  0x03 },
		{ KB8010_REG_SBBR_COMRX_CH_SHARED_LINK_CTRL_RUN_OFFSET, 0x07 },
		{ KB8010_REG_SBBR_BR_RX_CAL_VGA2_GXR, 0x04 },
		{ KB8010_REG_SBBR_COMRX_AZC_CTRL_CTLE_OC_BW_STG1, 0x03 },
		{ KB8010_REG_SBBR_COMRX_AZC_CTRL_CTLE_OC_BW_STG2, 0x06 },
		{ KB8010_REG_SBBR_COMRX_AZC_CTRL_CTLE_OC_BW_STG3, 0x0A },
		{ KB8010_REG_SBBR_COMRX_LFPS_LFPS_CTRL, 0x12 },
		{ KB8010_REG_SBBR_BR_RX_CAL_OFFSET_EYE_BG_SAT_OVF, 0x01 },
		{ KB8010_REG_SBBR_COMRX_CH_0_LINK_CTRL_RUN3, 0xFF },
		{ KB8010_REG_SBBR_COMRX_CH_1_LINK_CTRL_RUN3, 0xFF },
	};

	return kb8010_pair_write(me, kb8010_init_cfg,
				 ARRAY_SIZE(kb8010_init_cfg));
}

static int kb8010_config_usb4_tbt(const struct usb_mux *me,
				  mux_state_t mux_state)
{
	enum idh_ptype cable_type;
	uint8_t kb8k_cable_type;
	int rv;
	union tbt_mode_resp_cable cable_resp = {
		.raw_value =
			pd_get_tbt_mode_vdo(me->usb_port, TCPCI_MSG_SOP_PRIME)
	};

	cable_type = get_usb_pd_cable_type(me->usb_port);

	rv = kb8010_write(me, KB8010_REG_PROTOCOL, KB8010_PROTOCOL_USB4);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	rv = kb8010_write(me, KB8010_REG_CIO_CFG_WAKEUP_IGN_LS_DET, 0x1d);
	if (rv != EC_SUCCESS) {
		return rv;
	}
	rv = kb8010_write(me, 0x8185, 0x0a);
	rv = kb8010_write(me, 0x8257, 0xe3);

	const uint8_t r_806b[] = {
		0x40,
		0x40,
	};
	rv = kb8010_sequential_write(me, 0x806b, r_806b, ARRAY_SIZE(r_806b));

	/* Special configuration only for legacy mode */
	if (cable_type == IDH_PTYPE_ACABLE ||
	    cable_resp.tbt_active_passive == TBT_CABLE_ACTIVE) {
		/* Active cable */
		if (cable_resp.lsrx_comm == UNIDIR_LSRX_COMM) {
			/* uni-directional */
			kb8k_cable_type = KB8010_CABLE_TYPE_ACTIVE_UNIDIR;
		} else {
			/* bi-directional */
			kb8k_cable_type = KB8010_CABLE_TYPE_ACTIVE_BIDIR;
		}
	} else {
		/* Passive Cable */
		kb8k_cable_type = KB8010_CABLE_TYPE_PASSIVE;
	}

	rv = kb8010_write(me, KB8010_REG_ORIENTATION, kb8k_cable_type);
	if (rv != EC_SUCCESS) {
		return rv;
	}
	if (kb8k_cable_type == KB8010_CABLE_TYPE_ACTIVE_BIDIR) {
		rv = kb8010_write(me, 0x825a, 0x05);
		if (rv != EC_SUCCESS) {
			return rv;
		}
	}

	return EC_SUCCESS;
}

static int kb8010_set_state(const struct usb_mux *me, mux_state_t mux_state,
			    bool *ack_required)
{
	int rv;
	enum kb8010_modes mode;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	rv = kb8010_write(me, KB8010_REG_RESET, KB8010_RESET_MASK);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	/* Release memory map reset */
	rv = kb8010_write(me, KB8010_REG_RESET,
			  KB8010_RESET_MASK & ~KB8010_RESET_MM);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	/* Already in reset, nothing to do */
	if ((mux_state == USB_PD_MUX_NONE) ||
	    (mux_state & USB_PD_MUX_SAFE_MODE)) {
		return EC_SUCCESS;
	}

	/* Perform common initialization */
	rv = kb8010_common_init(me);
	if (rv != EC_SUCCESS) {
		return rv;
	}

	if (mux_state &
	    (USB_PD_MUX_USB4_ENABLED | USB_PD_MUX_TBT_COMPAT_ENABLED)) {
		mode = KB8010_USB4_TBT;
	} else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		if (mux_state & USB_PD_MUX_USB_ENABLED) {
			mode = KB8010_DPMF;
		} else {
			mode = KB8010_DP;
		}
	} else {
		mode = KB8010_USB3;
	}

	/* Flip configuration */
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED) {
		rv = kb8010_write(me, KB8010_REG_XBAR_SBU_CFG, 0x0d);
		if (rv != EC_SUCCESS) {
			return rv;
		}
		rv = kb8010_write(me, KB8010_REG_XBAR_OVR, 0x40);
		if (rv != EC_SUCCESS) {
			return rv;
		}
		rv = kb8010_sequential_write(me, KB8010_REG_XBAR_EB1SEL,
					     kb8010_flip_cfg[mode],
					     ARRAY_SIZE(kb8010_flip_cfg[mode]));
		if (rv != EC_SUCCESS) {
			return rv;
		}
	} else {
		rv = kb8010_write(me, KB8010_REG_XBAR_SBU_CFG, 0x02);
		if (rv != EC_SUCCESS) {
			return rv;
		}
	}

	if (mode == KB8010_USB4_TBT) {
		rv = kb8010_config_usb4_tbt(me, mux_state);
		if (rv != EC_SUCCESS) {
			return rv;
		}
	} else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		rv = kb8010_write(me, KB8010_REG_PROTOCOL, mode);
		if (rv != EC_SUCCESS) {
			return rv;
		}
		if (mode == KB8010_DPMF) {
			rv = kb8010_write(me, 0x9010, 0x05);
			if (rv != EC_SUCCESS) {
				return rv;
			}
		}

		/* DP and DPMF modes utilize the same DP configuration */
		rv = kb8010_pair_write(me, kb8010_dp_cfg,
				       ARRAY_SIZE(kb8010_dp_cfg));
		if (rv != EC_SUCCESS) {
			return rv;
		}
	}

	const int aux_lsx_control =
		kb8010_controls[me->usb_port].dp_enable_gpio;
	int aux_lsx_state = (mode == KB8010_DP) || (mode == KB8010_DPMF);

	gpio_or_ioex_set_level(aux_lsx_control, aux_lsx_state);

	/* Enable KB8010 */
	return kb8010_write(me, KB8010_REG_RESET, 0x00);
}

static int kb8010_init(const struct usb_mux *me)
{
	bool unused;

	gpio_set_level(kb8010_controls[me->usb_port].retimer_rst_gpio, 0);
	crec_msleep(KB8010_POWER_ON_DELAY_MS);

	gpio_set_level(kb8010_controls[me->usb_port].retimer_rst_gpio, 1);

	/*
	 * Delay after enabling power and releasing the reset to allow the power
	 * to come up and the reset to be released by the power sequencing
	 * logic. If after the delay, the reset is still held low - return an
	 * error.
	 */
	crec_msleep(KB8010_POWER_ON_DELAY_MS);
	if (!gpio_get_level(kb8010_controls[me->usb_port].retimer_rst_gpio)) {
		return EC_ERROR_NOT_POWERED;
	}

	return kb8010_set_state(me, USB_PD_MUX_NONE, &unused);
}

static int kb8010_enter_low_power_mode(const struct usb_mux *me)
{
	/* To endter low power mode, put KB8010 in reset */
	gpio_set_level(kb8010_controls[me->usb_port].retimer_rst_gpio, 0);

	return EC_SUCCESS;
}

const struct usb_mux_driver kb8010_usb_retimer_driver = {
	.init = kb8010_init,
	.set = kb8010_set_state,
	.enter_low_power_mode = kb8010_enter_low_power_mode,
};
