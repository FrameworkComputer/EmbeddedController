/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "board.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb.h"
#include "usb_bb.h"
#include "usb_pd.h"
#include "version.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS 0

/* Source PDOs */
const uint32_t pd_src_pdo[] = {};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

/* Define typical operating power and max power */
#define OPERATING_POWER_MW 1000
#define MAX_POWER_MW       1500
#define MAX_CURRENT_MA     300

/* Fake PDOs : we just want our pre-defined voltages */
const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000,   500, PDO_FIXED_FLAGS),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/* Desired voltage requested as a sink (in millivolts) */
static unsigned select_mv = 5000;

/* Whether alternate mode has been entered or not */
static int alt_mode;

int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo,
		      uint32_t *curr_limit, uint32_t *supply_voltage)
{
	int i;
	int ma;
	int set_mv = select_mv;
	int max;
	uint32_t flags;

	/* Default to 5V */
	if (set_mv <= 0)
		set_mv = 5000;

	/* Get the selected voltage */
	for (i = cnt; i >= 0; i--) {
		int mv = ((src_caps[i] >> 10) & 0x3FF) * 50;
		int type = src_caps[i] & PDO_TYPE_MASK;
		if ((mv == set_mv) && (type == PDO_TYPE_FIXED))
			break;
	}
	if (i < 0)
		return -EC_ERROR_UNKNOWN;

	/* build rdo for desired power */
	ma = 10 * (src_caps[i] & 0x3FF);
	max = MIN(ma, MAX_CURRENT_MA);
	flags = (max * set_mv) < (1000 * OPERATING_POWER_MW) ?
			RDO_CAP_MISMATCH : 0;
	*rdo = RDO_FIXED(i + 1, max, max, 0);
	CPRINTF("Request [%d] %dV %dmA", i, set_mv/1000, max);
	/* Mismatch bit set if less power offered than the operating power */
	if (flags & RDO_CAP_MISMATCH)
		CPRINTF(" Mismatch");
	CPRINTF("\n");

	*curr_limit = max;
	*supply_voltage = set_mv;
	return EC_SUCCESS;
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	/* No battery, nothing to do */
	return;
}

void pd_set_max_voltage(unsigned mv)
{
	select_mv = mv;
}

int requested_voltage_idx;
int pd_request_voltage(uint32_t rdo)
{
	return EC_SUCCESS;
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/* Always refuse power swap */
	return 0;
}

int pd_check_data_swap(int port, int data_role)
{
	/* Always refuse data swap */
	return 0;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Do nothing */
}

void pd_new_contract(int port, int pr_role, int dr_role,
		     int partner_pr_swap, int partner_dr_swap)
{
}
/* ----------------- Vendor Defined Messages ------------------ */
const uint32_t vdo_idh = VDO_IDH(0, /* data caps as USB host */
				 1, /* data caps as USB device */
				 IDH_PTYPE_AMA, /* Alternate mode */
				 1, /* supports alt modes */
				 USB_VID_GOOGLE);

const uint32_t vdo_product = VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

const uint32_t vdo_ama = VDO_AMA(CONFIG_USB_PD_IDENTITY_HW_VERS,
				 CONFIG_USB_PD_IDENTITY_SW_VERS,
				 0, 0, 0, 0, /* SS[TR][12] */
				 0, /* Vconn power */
				 0, /* Vconn power required */
				 1, /* Vbus power required */
				 AMA_USBSS_BBONLY /* USB SS support */);

static int svdm_response_identity(int port, uint32_t *payload)
{
	payload[VDO_I(IDH)] = vdo_idh;
	/* TODO(tbroch): Do we plan to obtain TID (test ID) for hoho */
	payload[VDO_I(CSTAT)] = VDO_CSTAT(0);
	payload[VDO_I(PRODUCT)] = vdo_product;
	payload[VDO_I(AMA)] = vdo_ama;
	return VDO_I(AMA) + 1;
}

static int svdm_response_svids(int port, uint32_t *payload)
{
	payload[1] = VDO_SVID(USB_SID_DISPLAYPORT, 0);
	return 2;
}

/*
 * Will only ever be a single mode for this UFP_D device as it has no real USB
 * support making it only PIN_E configureable
 */
#define MODE_CNT 1
#define OPOS 1

const uint32_t vdo_dp_mode[MODE_CNT] =  {
	VDO_MODE_DP(0,		   /* UFP pin cfg supported : none */
		    MODE_DP_PIN_E, /* DFP pin cfg supported */
		    1,		   /* no usb2.0	signalling in AMode */
		    CABLE_PLUG,	   /* its a plug */
		    MODE_DP_V13,   /* DPv1.3 Support, no Gen2 */
		    MODE_DP_SNK)   /* Its a sink only */
};

static int svdm_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT)
		return 0; /* nak */

	memcpy(payload + 1, vdo_dp_mode, sizeof(vdo_dp_mode));
	return MODE_CNT + 1;
}

static int dp_status(int port, uint32_t *payload)
{
	int opos = PD_VDO_OPOS(payload[0]);
	int hpd = gpio_get_level(GPIO_DP_HPD);
	if (opos != OPOS)
		return 0; /* nak */

	payload[1] = VDO_DP_STATUS(0,                /* IRQ_HPD */
				   (hpd == 1),       /* HPD_HI|LOW */
				   0,		     /* request exit DP */
				   0,		     /* request exit USB */
				   0,		     /* MF pref */
				   gpio_get_level(GPIO_PD_SBU_ENABLE),
				   0,		     /* power low */
				   0x2);
	return 2;
}

static int dp_config(int port, uint32_t *payload)
{
	if (PD_DP_CFG_DPON(payload[1]))
		gpio_set_level(GPIO_PD_SBU_ENABLE, 1);

	return 1;
}

static int svdm_enter_mode(int port, uint32_t *payload)
{
	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT) ||
	    (PD_VDO_OPOS(payload[0]) != OPOS))
		return 0; /* will generate NAK */

	/* TODO(tbroch) Enumerate USB BB here with updated mode choice */
	alt_mode = OPOS;
	return 1;
}

int pd_alt_mode(int port)
{
	return alt_mode;
}

static int svdm_exit_mode(int port, uint32_t *payload)
{
	gpio_set_level(GPIO_PD_SBU_ENABLE, 0);
	alt_mode = 0;
	return 1; /* Must return ACK */
}

static struct amode_fx dp_fx = {
	.status = &dp_status,
	.config = &dp_config,
};

const struct svdm_response svdm_rsp = {
	.identity = &svdm_response_identity,
	.svids = &svdm_response_svids,
	.modes = &svdm_response_modes,
	.enter_mode = &svdm_enter_mode,
	.amode = &dp_fx,
	.exit_mode = &svdm_exit_mode,
};

static int pd_custom_vdm(int port, int cnt, uint32_t *payload,
			 uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	int rsize = 1;
	CPRINTF("VDM/%d [%d] %08x\n", cnt, cmd, payload[0]);

	*rpayload = payload;
	switch (cmd) {
	case VDO_CMD_VERSION:
		memcpy(payload + 1, &version_data.version, 24);
		rsize = 7;
		break;
	default:
		rsize = 0;
	}

	CPRINTS("DONE");
	/* respond (positively) to the request */
	payload[0] |= VDO_SRC_RESPONDER;

	return rsize;
}

int pd_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	if (PD_VDO_SVDM(payload[0]))
		return pd_svdm(port, cnt, payload, rpayload);
	else
		return pd_custom_vdm(port, cnt, payload, rpayload);
}
