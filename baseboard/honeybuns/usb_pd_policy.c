/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "chip/stm32/ucpd-stm32gx.h"
#include "cros_board_info.h"
#include "driver/mp4245.h"
#include "driver/tcpm/tcpci.h"
#include "driver/mp4245.h"
#include "task.h"
#include "timer.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dp_ufp.h"
#include "usbc_ppc.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |\
			 PDO_FIXED_COMM_CAP | PDO_FIXED_UNCONSTRAINED)

/* Voltage indexes for the PDOs */
enum volt_idx {
	PDO_IDX_5V   = 0,
	PDO_IDX_9V   = 1,
	PDO_IDX_15V  = 2,
	PDO_IDX_20V  = 3,
	PDO_IDX_COUNT
};

/* PDOs */
const uint32_t pd_src_host_pdo[] = {
	[PDO_IDX_5V]  = PDO_FIXED(5000,   3000, PDO_FIXED_FLAGS),
	[PDO_IDX_9V]  = PDO_FIXED(9000,   3000, 0),
	[PDO_IDX_15V]  = PDO_FIXED(15000, 3000, 0),
	[PDO_IDX_20V]  = PDO_FIXED(20000, 3000, 0),
};
BUILD_ASSERT(ARRAY_SIZE(pd_src_host_pdo) == PDO_IDX_COUNT);

const uint32_t pd_src_display_pdo[] = {
	[PDO_IDX_5V]  = PDO_FIXED(5000,   3000, PDO_FIXED_FLAGS),
};

const uint32_t pd_snk_pdo[] = {
	[PDO_IDX_5V]  = PDO_FIXED(5000,   0, PDO_FIXED_FLAGS),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

int dpm_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	int pdo_cnt = 0;

	if (port == USB_PD_PORT_HOST) {
		*src_pdo =  pd_src_host_pdo;
		pdo_cnt = ARRAY_SIZE(pd_src_host_pdo);
	} else {
		*src_pdo =  pd_src_display_pdo;
		pdo_cnt = ARRAY_SIZE(pd_src_display_pdo);
	}

	return pdo_cnt;
}

/*
 * Default Port Discovery DR Swap Policy.
 *
 * 1) If port == 0 and port data role is DFP, transition to pe_drs_send_swap
 * 2) If port == 1 and port data role is UFP, transition to pe_drs_send_swap
 */
__override bool port_discovery_dr_swap_policy(int port,
			enum pd_data_role dr, bool dr_swap_flag)
{
	/*
	 * Port0: test if role is DFP
	 * Port1: test if role is UFP
	 */
	enum pd_data_role role_test =
		(port == USB_PD_PORT_HOST) ? PD_ROLE_DFP : PD_ROLE_UFP;

	if (dr == role_test)
		return true;

	/* Do not perform a DR swap */
	return false;
}

/*
 * Default Port Discovery VCONN Swap Policy.
 *
 * 1) Never perform VCONN swap
 */
__override bool port_discovery_vconn_swap_policy(int port,
			bool vconn_swap_flag)
{
	/* Do not perform a VCONN swap */
	return false;
}

int pd_check_vconn_swap(int port)
{
	/*TODO: Dock is the Vconn source */
	return 1;
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	if (port < 0 || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return;

	prev_en = ppc_is_sourcing_vbus(port);

	/* Disable VBUS. */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

	if (port == USB_PD_PORT_HOST) {
		/* Turn off voltage output from buck-boost */
		mp4245_votlage_out_enable(0);
		/* Reset VBUS voltage to default value (fixed 5V SRC_CAP) */
		pd_transition_voltage(1);
	}
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	if (port == USB_PD_PORT_HOST) {
		/* Ensure buck-boost is enabled and Vout is on */
		mp4245_votlage_out_enable(1);
		msleep(MP4245_VOUT_5V_DELAY_MS);
	}

	/*
	 * Default operation of buck-boost is 5v/3.6A.
	 * Turn on the PPC Provide Vbus.
	 */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

void pd_transition_voltage(int idx)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	if (port == USB_PD_PORT_HOST) {
		int mv, unused_mv;
		int ma;
		int vbus_hi;
		int vbus_lo;
		int i;

	/*
	 * Set the VBUS output voltage and current limit to the values specified
	 * by the PDO requested by sink. Note that USB PD uses idx = 1 for 1st
	 * PDO of SRC_CAP which must always be 5V fixed supply.
	 */
		pd_extract_pdo_power(pd_src_host_pdo[idx - 1], &ma, &mv,
				     &unused_mv);

		/* Set VBUS level to value specified in the requested PDO */
		mp4245_set_voltage_out(mv);
		/* Wait for vbus to be within ~5% of its target value */
		vbus_hi = mv + (mv >> 4);
		vbus_lo = mv - (mv >> 4);

		for (i = 0; i < 20; i++) {
			int rv;

			rv =  mp3245_get_vbus(&mv, &ma);
			if ((rv == EC_SUCCESS) && (mv >= vbus_lo) &&
			    (mv <= vbus_hi))
				return;

			msleep(2);
		}
	}
}

int pd_snk_is_vbus_provided(int port)
{
	return ppc_is_vbus_present(port);
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{

}

int pd_check_data_swap(int port,
	enum pd_data_role data_role)
{
	int swap = 0;

	if (port == 0)
		swap = (data_role == PD_ROLE_DFP);
	else if (port == 1)
		swap = (data_role == PD_ROLE_UFP);

	return swap;
}

int pd_check_power_swap(int port)
{

	if (pd_get_power_role(port) == PD_ROLE_SINK)
		return 1;

	return 0;
}

static int vdm_is_dp_enabled(int port)
{
	mux_state_t mux_state = usb_mux_get(port);

	return !!(mux_state & USB_PD_MUX_DP_ENABLED);
}

/* ----------------- Vendor Defined Messages ------------------ */

const uint32_t vdo_idh = VDO_IDH(0, /* data caps as USB host */
				 1, /* data caps as USB device */
				 IDH_PTYPE_HUB, /* UFP product type usbpd hub */
				 1, /* supports alt modes */
				 USB_VID_GOOGLE);

static const uint32_t vdo_idh_rev30 = VDO_IDH_REV30(
				 0, /* Data caps as USB host     */
				 1, /* Data caps as USB device   */
				 IDH_PTYPE_HUB,
				 1, /* Supports alt modes */
				 IDH_PTYPE_DFP_UNDEFINED,
				 USB_TYPEC_RECEPTACLE,
				 USB_VID_GOOGLE);

const uint32_t vdo_product = VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

static const uint32_t vdo_ufp1 = VDO_UFP1(
				   (VDO_UFP1_CAPABILITY_USB20
				   | VDO_UFP1_CAPABILITY_USB32),
				   USB_TYPEC_RECEPTACLE,
				   VDO_UFP1_ALT_MODE_RECONFIGURE,
				   USB_R30_SS_U32_U40_GEN2);

static int svdm_response_identity(int port, uint32_t *payload)
{
	int vdo_count;

	/* Verify that SVID is PD SID */
	if (PD_VDO_VID(payload[0]) != USB_SID_PD) {
		return 0;
	}

	/* Cstat and Product VDOs don't depend on spec revision */
	payload[VDO_INDEX_CSTAT] = VDO_CSTAT(0);
	payload[VDO_INDEX_PRODUCT] = vdo_product;

	if (pd_get_rev(port, TCPC_TX_SOP) == PD_REV30) {
		/* PD Revision 3.0 */
		payload[VDO_INDEX_IDH] = vdo_idh_rev30;
		payload[VDO_INDEX_PTYPE_UFP1_VDO] = vdo_ufp1;
		vdo_count = VDO_INDEX_PTYPE_UFP1_VDO;
	} else {
		payload[VDO_INDEX_IDH] = vdo_idh;
		vdo_count = VDO_INDEX_PRODUCT;
	}

	/* Adjust VDO count for VDM header */
	return vdo_count + 1;
}

static int svdm_response_svids(int port, uint32_t *payload)
{
	/* Verify that SVID is PD SID */
	if (PD_VDO_VID(payload[0]) != USB_SID_PD) {
		return 0;
	}

	payload[1] = USB_SID_DISPLAYPORT << 16;
	/* number of data objects VDO header + 1 SVID for DP */
	return 2;
}

#define OPOS_DP 1

const uint32_t vdo_dp_modes[1] =  {
	VDO_MODE_DP(/* Must support C and E. D is required for 2 lanes */
		    MODE_DP_PIN_C | MODE_DP_PIN_D | MODE_DP_PIN_D,
		    0, /* DFP pin cfg supported */
		    1,		   /* no usb2.0	signalling in AMode */
		    CABLE_RECEPTACLE,	   /* its a receptacle */
		    MODE_DP_V13,   /* DPv1.3 Support, no Gen2 */
		    MODE_DP_SNK)   /* Its a sink only */
};

static int svdm_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT) {
		memcpy(payload + 1, vdo_dp_modes, sizeof(vdo_dp_modes));
		return ARRAY_SIZE(vdo_dp_modes) + 1;
	} else {
		return 0; /* nak */
	}
}

static int amode_dp_status(int port, uint32_t *payload)
{
	int opos = PD_VDO_OPOS(payload[0]);
	int hpd = gpio_get_level(GPIO_DP_HPD);
	int mf = dock_get_mf_preference();

	if (opos != OPOS_DP)
		return 0; /* nak */

	payload[1] = VDO_DP_STATUS(0,		/* IRQ_HPD */
				   (hpd == 1),	/* HPD_HI|LOW */
				   0,		/* request exit DP */
				   0,		/* request exit USB */
				   mf,		/* MF pref */
				   vdm_is_dp_enabled(port),
				   0,		/* power low */
				   0x2);
	return 2;
}

static void svdm_configure_demux(int port, int enable, int mf)
{
	mux_state_t demux = usb_mux_get(port);

	if (enable) {
		demux |= USB_PD_MUX_DP_ENABLED;
		/* 4 lane mode if MF is not preferred */
		if (!mf)
			demux &= ~USB_PD_MUX_USB_ENABLED;
		/*
		 * Make sure the MST_LANE_CONTROL gpio is set to match the DP
		 * pin configuration selected by the host. Note that the mf
		 * passed into this function reflects the pin configuration
		 * selected by the host and not the user mf preference which is
		 * stored in bit 0 of CBI fw_config.
		 */
		baseboard_set_mst_lane_control(mf);
	} else {
		demux &= ~USB_PD_MUX_DP_ENABLED;
		demux |= USB_PD_MUX_USB_ENABLED;
	}

	/* Configure demux for 2/4 lane DP and USB3 configuration */
	usb_mux_set(port, demux, USB_SWITCH_CONNECT, pd_get_polarity(port));
}

static int amode_dp_config(int port, uint32_t *payload)
{
	uint32_t dp_config = payload[1];
	int mf;

	/*
	 * Check pin assignment selected by DFP_D to determine if 2 lane or 4
	 * lane DP ALT-MODe is required. (note PIN_C is for 4 lane and PIN_D is
	 * for 2 lane mode).
	 */
	mf = ((dp_config >> 8) & 0xff) == MODE_DP_PIN_D ? 1 : 0;
	/* Configure demux for DP mode */
	svdm_configure_demux(port, 1, mf);

	return 1;
}

static int svdm_enter_mode(int port, uint32_t *payload)
{
	int rv = 0; /* will generate a NAK */

	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT) &&
	    (PD_VDO_OPOS(payload[0]) == OPOS_DP)) {

		/* Store valid object position to indicate mode is active */
		pd_ufp_set_dp_opos(port, OPOS_DP);

		/* Entering ALT-DP mode, enable DP connection in demux */
		usb_pd_hpd_converter_enable(1);

		/* ACK response has 1 VDO */
		rv = 1;
	}

	CPRINTS("svdm_enter[%d]: svid = %x, ret = %d", port,
		PD_VDO_VID(payload[0]), rv);

	return rv;
}

static int svdm_exit_mode(int port, uint32_t *payload)
{
	int opos = pd_ufp_get_dp_opos(port);

	if ((PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT) &&
	    (opos == OPOS_DP)) {
		/* Clear mode active object position */
		pd_ufp_set_dp_opos(port, 0);
		/* Configure demux to disable DP mode */
		svdm_configure_demux(port, 0, 0);
		usb_pd_hpd_converter_enable(0);

		return 1;
	} else {
		CPRINTF("Unknown exit mode req:0x%08x\n", payload[0]);
		return 0;
	}
}

static struct amode_fx dp_fx = {
	.status = &amode_dp_status,
	.config = &amode_dp_config,
};

const struct svdm_response svdm_rsp = {
	.identity = &svdm_response_identity,
	.svids = &svdm_response_svids,
	.modes = &svdm_response_modes,
	.enter_mode = &svdm_enter_mode,
	.amode = &dp_fx,
	.exit_mode = &svdm_exit_mode,
};

int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	/* We don't support, so ignore this message */
	return 0;
}
