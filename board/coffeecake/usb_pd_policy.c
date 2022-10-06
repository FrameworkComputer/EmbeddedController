/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "board.h"
#include "charger/sy21612.h"
#include "common.h"
#include "console.h"
#include "cros_version.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb_api.h"
#include "usb_bb.h"
#include "usb_pd.h"
#include "usb_pd_pdo.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* Holds valid object position (opos) for entered mode */
static int alt_mode[PD_AMODE_COUNT];

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

void pd_transition_voltage(int idx)
{
	/* TODO: discharge, PPS */
	switch (idx - 1) {
	case PDO_IDX_9V:
		board_set_usb_output_voltage(9000);
		break;
	case PDO_IDX_5V:
	default:
		board_set_usb_output_voltage(5000);
		break;
	}
}

int pd_set_power_supply_ready(int port)
{
	/* Turn on DAC and adjust feedback to get 5V output */
	board_set_usb_output_voltage(5000);
	/* Enable Vsys to USBC Vbus charging */
	sy21612_set_sink_mode(1);
	sy21612_set_adc_mode(1);
	sy21612_enable_adc(1);
	sy21612_set_vbus_discharge(0);
	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
	/* Turn off DAC output */
	board_set_usb_output_voltage(-1);
	/* Turn off USBC VBUS output */
	sy21612_set_sink_mode(0);
	/* Set boost Vsys output 9V */
	sy21612_set_vbus_volt(SY21612_VBUS_9V);
	/* Turn on buck-boost converter ADC */
	sy21612_set_adc_mode(1);
	sy21612_enable_adc(1);
	sy21612_set_vbus_discharge(1);
}

int pd_snk_is_vbus_provided(int port)
{
	return 1;
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

int pd_check_data_swap(int port, enum pd_data_role data_role)
{
	/* We can swap to UFP */
	return data_role == PD_ROLE_DFP;
}

void pd_execute_data_swap(int port, enum pd_data_role data_role)
{
	/* TODO: turn on pp5000, pp3300 */
}

void pd_check_pr_role(int port, enum pd_power_role pr_role, int flags)
{
	if (pr_role == PD_ROLE_SINK && !gpio_get_level(GPIO_AC_PRESENT_L))
		pd_request_power_swap(port);
}

void pd_check_dr_role(int port, enum pd_data_role dr_role, int flags)
{
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_DFP)
		pd_request_data_swap(port);
}
/* ----------------- Vendor Defined Messages ------------------ */
const uint32_t vdo_idh = VDO_IDH(0, /* data caps as USB host */
				 1, /* data caps as USB device */
				 IDH_PTYPE_AMA, /* Alternate mode */
				 1, /* supports alt modes */
				 USB_VID_GOOGLE);

const uint32_t vdo_product = VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

const uint32_t vdo_ama = VDO_AMA(CONFIG_USB_PD_IDENTITY_HW_VERS,
				 CONFIG_USB_PD_IDENTITY_SW_VERS, 0, 0, 0,
				 0, /* SS[TR][12] */
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
	payload[1] = VDO_SVID(USB_SID_DISPLAYPORT, USB_VID_GOOGLE);
	payload[2] = 0;
	return 3;
}

#define OPOS_DP 1
#define OPOS_GFU 1

const uint32_t vdo_dp_modes[1] = {
	VDO_MODE_DP(0, /* UFP pin cfg supported : none */
		    MODE_DP_PIN_C, /* DFP pin cfg supported */
		    1, /* no usb2.0	signalling in AMode */
		    CABLE_PLUG, /* its a plug */
		    MODE_DP_V13, /* DPv1.3 Support, no Gen2 */
		    MODE_DP_SNK) /* Its a sink only */
};

const uint32_t vdo_goog_modes[1] = { VDO_MODE_GOOGLE(MODE_GOOGLE_FU) };

static int svdm_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT) {
		memcpy(payload + 1, vdo_dp_modes, sizeof(vdo_dp_modes));
		return ARRAY_SIZE(vdo_dp_modes) + 1;
	} else if (PD_VDO_VID(payload[0]) == USB_VID_GOOGLE) {
		memcpy(payload + 1, vdo_goog_modes, sizeof(vdo_goog_modes));
		return ARRAY_SIZE(vdo_goog_modes) + 1;
	} else {
		return 0; /* nak */
	}
}

static int dp_status(int port, uint32_t *payload)
{
	int opos = PD_VDO_OPOS(payload[0]);
	int hpd = gpio_get_level(GPIO_DP_HPD);
	if (opos != OPOS_DP)
		return 0; /* nak */

	payload[1] = VDO_DP_STATUS(0, /* IRQ_HPD */
				   (hpd == 1), /* HPD_HI|LOW */
				   0, /* request exit DP */
				   0, /* request exit USB */
				   0, /* MF pref */
				   gpio_get_level(GPIO_PD_SBU_ENABLE),
				   0, /* power
					 low
				       */
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
	int rv = 0; /* will generate a NAK */

	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT) &&
	    (PD_VDO_OPOS(payload[0]) == OPOS_DP)) {
		alt_mode[PD_AMODE_DISPLAYPORT] = OPOS_DP;
		rv = 1;
		pd_log_event(PD_EVENT_VIDEO_DP_MODE, 0, 1, NULL);
	} else if ((PD_VDO_VID(payload[0]) == USB_VID_GOOGLE) &&
		   (PD_VDO_OPOS(payload[0]) == OPOS_GFU)) {
		alt_mode[PD_AMODE_GOOGLE] = OPOS_GFU;
		rv = 1;
	}

	if (rv)
		/*
		 * If we failed initial mode entry we'll have enumerated the USB
		 * Billboard class.  If so we should disconnect.
		 */
		usb_disconnect();

	return rv;
}

int pd_alt_mode(int port, enum tcpci_msg_type type, uint16_t svid)
{
	if (type != TCPCI_MSG_SOP)
		return 0;

	if (svid == USB_SID_DISPLAYPORT)
		return alt_mode[PD_AMODE_DISPLAYPORT];
	else if (svid == USB_VID_GOOGLE)
		return alt_mode[PD_AMODE_GOOGLE];
	return 0;
}

static int svdm_exit_mode(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT) {
		gpio_set_level(GPIO_PD_SBU_ENABLE, 0);
		alt_mode[PD_AMODE_DISPLAYPORT] = 0;
		pd_log_event(PD_EVENT_VIDEO_DP_MODE, 0, 0, NULL);
	} else if (PD_VDO_VID(payload[0]) == USB_VID_GOOGLE) {
		alt_mode[PD_AMODE_GOOGLE] = 0;
	} else {
		CPRINTF("Unknown exit mode req:0x%08x\n", payload[0]);
	}

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
	int rsize;

	if (PD_VDO_VID(payload[0]) != USB_VID_GOOGLE ||
	    !alt_mode[PD_AMODE_GOOGLE])
		return 0;

	*rpayload = payload;

	rsize = pd_custom_flash_vdm(port, cnt, payload);
	if (!rsize) {
		int cmd = PD_VDO_CMD(payload[0]);
		switch (cmd) {
		case VDO_CMD_GET_LOG:
			rsize = pd_vdm_get_log_entry(payload);
			break;
		default:
			/* Unknown : do not answer */
			return 0;
		}
	}

	/* respond (positively) to the request */
	payload[0] |= VDO_SRC_RESPONDER;

	return rsize;
}
