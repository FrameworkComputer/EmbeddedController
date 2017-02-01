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

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Acceptable margin between requested VBUS and measured value */
#define MARGIN_MV 400 /* mV */

#define PDO_FIXED_FLAGS (PDO_FIXED_DATA_SWAP | PDO_FIXED_EXTERNAL |\
			 PDO_FIXED_COMM_CAP)

/* Source PDOs */
const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,  3000, PDO_FIXED_FLAGS),
		PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(20000, 3000, PDO_FIXED_FLAGS),
};
static const int pd_src_pdo_cnts[3] = {
		[SRC_CAP_5V] = 1,
		[SRC_CAP_12V] = 2,
		[SRC_CAP_20V] = 3,
};

static int pd_src_pdo_idx;

/* Fake PDOs : we just want our pre-defined voltages */
const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000,   500, PDO_FIXED_FLAGS),
		PDO_FIXED(12000,  500, PDO_FIXED_FLAGS),
		PDO_FIXED(20000,  500, PDO_FIXED_FLAGS),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/* Whether alternate mode has been entered or not */
static int alt_mode;

void board_set_source_cap(enum board_src_cap cap)
{
	pd_src_pdo_idx = cap;
}

int charge_manager_get_source_pdo(const uint32_t **src_pdo)
{
	*src_pdo = pd_src_pdo;
	return pd_src_pdo_cnts[pd_src_pdo_idx];
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	/* No battery, nothing to do */
	return;
}

int pd_is_valid_input_voltage(int mv)
{
	/* Any voltage less than the max is allowed */
	return 1;
}

int pd_board_check_request(uint32_t rdo, int pdo_cnt)
{
	int idx = RDO_POS(rdo);

	/* Check for invalid index */
	return (!idx || idx > pdo_cnt) ?
		EC_ERROR_INVAL : EC_SUCCESS;
}

void pd_transition_voltage(int idx)
{
	gpio_set_level(GPIO_USBC_VSEL_0, idx >= 2);
	gpio_set_level(GPIO_USBC_VSEL_1, idx >= 3);
}

int pd_set_power_supply_ready(int port)
{
	/* Output the correct voltage */
	gpio_set_level(GPIO_VBUS_CHARGER_EN, 1);

	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
	gpio_set_level(GPIO_VBUS_CHARGER_EN, 0);
	gpio_set_level(GPIO_USBC_VSEL_0, 0);
	gpio_set_level(GPIO_USBC_VSEL_1, 0);
}

int pd_snk_is_vbus_provided(int port)
{
	return gpio_get_level(GPIO_VBUS_WAKE);
}

int pd_board_checks(void)
{
	static int was_connected = -1;
	if (was_connected != 1 && pd_is_connected(0))
		board_maybe_reset_usb_hub();
	was_connected = pd_is_connected(0);
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/* Always allow power swap */
	return 1;
}

int pd_check_data_swap(int port, int data_role)
{
	/* Always allow data swap */
	return 1;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Do nothing */
}

void pd_check_pr_role(int port, int pr_role, int flags)
{
}

void pd_check_dr_role(int port, int dr_role, int flags)
{
	/* If Plankton is in USB hub mode, always act as UFP */
	if (board_in_hub_mode() && dr_role == PD_ROLE_DFP &&
	    (flags & PD_FLAGS_PARTNER_DR_DATA))
		pd_request_data_swap(port);
}

/* ----------------- Vendor Defined Messages ------------------ */
const uint32_t vdo_idh = VDO_IDH(0, /* data caps as USB host */
				 0, /* data caps as USB device */
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
	if (gpio_get_level(GPIO_USBC_SS_USB_MODE))
		return 0; /* nak */

	if (PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT)
		return 0; /* nak */

	memcpy(payload + 1, vdo_dp_mode, sizeof(vdo_dp_mode));
	return MODE_CNT + 1;
}

static int dp_status(int port, uint32_t *payload)
{
	int opos = PD_VDO_OPOS(payload[0]);
	int hpd = gpio_get_level(GPIO_DPSRC_HPD);
	if (opos != OPOS)
		return 0; /* nak */

	payload[1] = VDO_DP_STATUS(0,                /* IRQ_HPD */
				   (hpd == 1),       /* HPD_HI|LOW */
				   0,		     /* request exit DP */
				   0,		     /* request exit USB */
				   0,		     /* MF pref */
				   !gpio_get_level(GPIO_USBC_SS_USB_MODE),
				   0,		     /* power low */
				   0x2);
	return 2;
}

static int dp_config(int port, uint32_t *payload)
{
	if (PD_DP_CFG_DPON(payload[1]))
		gpio_set_level(GPIO_USBC_SS_USB_MODE, 0);
	return 1;
}

static int svdm_enter_mode(int port, uint32_t *payload)
{
	int usb_mode = gpio_get_level(GPIO_USBC_SS_USB_MODE);

	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT) ||
	    (PD_VDO_OPOS(payload[0]) != OPOS))
		return 0; /* will generate NAK */

	if (usb_mode) {
		CPRINTS("Toggle USB_MODE if you want DP & re-connect");
		return 0;
	}

	alt_mode = OPOS;
	return 1;
}

int pd_alt_mode(int port, uint16_t svid)
{
	return alt_mode;
}

static int svdm_exit_mode(int port, uint32_t *payload)
{
	alt_mode = 0;
	/*
	 * Don't actually toggle GPIO_USBC_SS_USB_MODE since its manually
	 * controlled by operator.
	 */
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

int pd_custom_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
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
