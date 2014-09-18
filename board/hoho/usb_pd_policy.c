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
#include "usb_pd.h"
#include "version.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Source PDOs */
const uint32_t pd_src_pdo[] = {};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

/* Fake PDOs : we just want our pre-defined voltages */
const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, 0),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/* Desired voltage requested as a sink (in millivolts) */
static unsigned select_mv = 5000;

int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo)
{
	int i;
	int ma;
	int set_mv = select_mv;

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

	/* request all the power ... */
	ma = 10 * (src_caps[i] & 0x3FF);
	*rdo = RDO_FIXED(i + 1, ma, ma, 0);
	ccprintf("Request [%d] %dV %dmA\n", i, set_mv/1000, ma);
	return ma;
}

void pd_set_input_current_limit(uint32_t max_ma)
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

/* ----------------- Vendor Defined Messages ------------------ */
const uint32_t vdo_idh = VDO_IDH(0, /* data caps as USB host */
				 0, /* data caps as USB device */
				 IDH_PTYPE_AMA, /* Alternate mode */
				 1, /* supports alt modes */
				 USB_VID_GOOGLE);

const uint32_t vdo_ama = VDO_AMA(CONFIG_USB_PD_IDENTITY_HW_VERS,
				 CONFIG_USB_PD_IDENTITY_SW_VERS,
				 0, 0, 0, 0, /* SS[TR][12] */
				 0, /* Vconn power */
				 0, /* Vconn power required */
				 1, /* Vbus power required */
				 0	 /* USB SS support */);

static int svdm_response_identity(int port, uint32_t *payload)
{
	payload[VDO_I(IDH)] = vdo_idh;
	/* TODO(tbroch): Do we plan to obtain TID (test ID) for hoho */
	payload[VDO_I(CSTAT)] = VDO_CSTAT(0);
	payload[VDO_I(AMA)] = vdo_ama;
	return 4;
}

static int svdm_response_svids(int port, uint32_t *payload)
{
	payload[1] = VDO_SVID(USB_SID_DISPLAYPORT, 0);
	return 2;
}

const uint32_t vdo_dp_mode[1] =  {
	VDO_MODE_DP(MODE_DP_PIN_E, /* sink pin cfg */
		    0,		   /* no src pin cfg */
		    1,		   /* no usb2.0	signalling in AMode */
		    CABLE_PLUG,	   /* its a plug */
		    0,		   /* no GEN2 usb */
		    0,		   /* no dp 1.3 support */
		    MODE_DP_SNK)   /* Its a sink only */
};

static int svdm_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT) {
		/* TODO(tbroch) USB billboard enabled here then */
		return 1; /* will generate a NAK */
	}
	memset(payload + 1, 0, sizeof(uint32_t) * PDO_MODES);
	payload[1] = vdo_dp_mode[0];
	/* TODO(tbroch) does spec have mechanism for identifying valid modes
	 * returned for svid? */
	return PDO_MAX_OBJECTS;
}

static int svdm_enter_mode(int port, uint32_t *payload)
{
	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT) ||
	    (PD_VDO_OPOS(payload[0]) != 1))
		return 1; /* will generate a NAK */

	gpio_set_level(GPIO_PD_SBU_ENABLE, 1);
	payload[1] = 0;
	return 1;
}

static int svdm_exit_mode(int port, uint32_t *payload)
{
	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT) ||
	    (PD_VDO_OPOS(payload[0]) != 1))
		return 1; /* will generate a NAK */

	gpio_set_level(GPIO_PD_SBU_ENABLE, 0);
	payload[1] = 0;
	return 1;
}

const struct svdm_response svdm_rsp = {
	.identity = &svdm_response_identity,
	.svids = &svdm_response_svids,
	.modes = &svdm_response_modes,
	.enter_mode = &svdm_enter_mode,
	.exit_mode = &svdm_exit_mode,
};

static int pd_custom_vdm(int port, int cnt, uint32_t *payload,
			 uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	int rsize = 1;
	ccprintf("%T] VDM/%d [%d] %08x\n", cnt, cmd, payload[0]);

	*rpayload = payload;
	switch (cmd) {
	case VDO_CMD_VERSION:
		memcpy(payload + 1, &version_data.version, 24);
		rsize = 7;
		break;
	default:
		/* Unknown : do not answer */
		return 0;
	}
	ccprintf("%T] DONE\n");
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
