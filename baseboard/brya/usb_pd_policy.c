/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared USB-C policy for Brya boards */

#include <stddef.h>
#include <stdint.h>

#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "usbc_ppc.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_vdo.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

int pd_check_vconn_swap(int port)
{
	/* Only allow vconn swap after the PP5000_Z1 rail is enabled */
	return gpio_get_level(GPIO_SEQ_EC_DSW_PWROK);
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = ppc_is_sourcing_vbus(port);

	/* Disable VBUS. */
	ppc_vbus_source_enable(port, 0);

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	/* Disable charging. */
	rv = ppc_vbus_sink_enable(port, 0);
	if (rv)
		return rv;

	pd_set_vbus_discharge(port, 0);

	/* Provide Vbus. */
	rv = ppc_vbus_source_enable(port, 1);
	if (rv)
		return rv;

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}

/* ----------------- Vendor Defined Messages ------------------ */
/* Responses specifically for the enablement of TBT mode in the role of UFP */

#define OPOS_TBT 1

static const union tbt_mode_resp_device vdo_tbt_modes[1] = {
		{
			.tbt_alt_mode = 0x0001,
			.tbt_adapter = TBT_ADAPTER_TBT3,
			.intel_spec_b0 = 0,
			.vendor_spec_b0 = 0,
			.vendor_spec_b1 = 0,
		}
};

static const uint32_t vdo_idh = VDO_IDH(
				1, /* Data caps as USB host     */
				0, /* Not a USB device   */
				IDH_PTYPE_PERIPH,
				1, /* Supports alt modes */
				USB_VID_GOOGLE);

static const uint32_t vdo_idh_rev30 = VDO_IDH_REV30(
				 1, /* Data caps as USB host     */
				 0, /* Not a USB device   */
				 IDH_PTYPE_PERIPH,
				 1, /* Supports alt modes */
				 IDH_PTYPE_DFP_HOST,
				 USB_TYPEC_RECEPTACLE,
				 USB_VID_GOOGLE);

static const uint32_t vdo_product = VDO_PRODUCT(CONFIG_USB_PID,
						CONFIG_USB_BCD_DEV);

/* TODO(b/168890624): add USB4 to capability once USB4 response implemented */
static const uint32_t vdo_ufp1 = VDO_UFP1(
				   (VDO_UFP1_CAPABILITY_USB20
				   | VDO_UFP1_CAPABILITY_USB32),
				   USB_TYPEC_RECEPTACLE,
				   VDO_UFP1_ALT_MODE_TBT3,
				   USB_R30_SS_U40_GEN3);

static const uint32_t vdo_dfp = VDO_DFP(
				 (VDO_DFP_HOST_CAPABILITY_USB20
				 | VDO_DFP_HOST_CAPABILITY_USB32
				 | VDO_DFP_HOST_CAPABILITY_USB4),
				 USB_TYPEC_RECEPTACLE,
				 1  /* Port 1 */);

static int svdm_tbt_compat_response_identity(int port, uint32_t *payload)
{
	/* TODO(b/154962766): Get an XID */
	payload[VDO_I(CSTAT)] = VDO_CSTAT(0);
	payload[VDO_I(PRODUCT)] = vdo_product;

	if (pd_get_rev(port, TCPC_TX_SOP) == PD_REV30) {
		/* PD Revision 3.0 */
		payload[VDO_I(IDH)] = vdo_idh_rev30;
		payload[VDO_I(PTYPE_UFP1_VDO)] = vdo_ufp1;
		/* TODO(b/181620145): Customize for brya */
		payload[VDO_I(PTYPE_UFP2_VDO)] = 0;
		payload[VDO_I(PTYPE_DFP_VDO)] = vdo_dfp;
		return VDO_I(PTYPE_DFP_VDO) + 1;
	}

	/* PD Revision 2.0 */
	payload[VDO_I(IDH)] = vdo_idh;
	return VDO_I(PRODUCT) + 1;
}

static int svdm_tbt_compat_response_svids(int port, uint32_t *payload)
{
	payload[1] = VDO_SVID(USB_VID_INTEL, 0);
	return 2;
}

static int svdm_tbt_compat_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) == USB_VID_INTEL) {
		memcpy(payload + 1, vdo_tbt_modes, sizeof(vdo_tbt_modes));
		return ARRAY_SIZE(vdo_tbt_modes) + 1;
	} else {
		return 0; /* NAK */
	}
}

static int svdm_tbt_compat_response_enter_mode(
	int port, uint32_t *payload)
{
	mux_state_t mux_state = 0;

	/* Do not enter mode while CPU is off. */
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF))
		return 0; /* NAK */

	if ((PD_VDO_VID(payload[0]) != USB_VID_INTEL) ||
		(PD_VDO_OPOS(payload[0]) != OPOS_TBT))
		return 0; /* NAK */

	mux_state = usb_mux_get(port);
	/*
	 * Ref: USB PD 3.0 Spec figure 6-21 Successful Enter Mode sequence
	 * UFP (responder) should be in USB mode or safe mode before sending
	 * Enter Mode Command response.
	 */
	if ((mux_state & USB_PD_MUX_USB_ENABLED) ||
		(mux_state & USB_PD_MUX_SAFE_MODE)) {
		pd_ufp_set_enter_mode(port, payload);
		set_tbt_compat_mode_ready(port);
		CPRINTS("UFP Enter TBT mode");
		return 1; /* ACK */
	}

	CPRINTS("UFP failed to enter TBT mode(mux=0x%x)", mux_state);
	return 0;
}

const struct svdm_response svdm_rsp = {
	.identity = &svdm_tbt_compat_response_identity,
	.svids = &svdm_tbt_compat_response_svids,
	.modes = &svdm_tbt_compat_response_modes,
	.enter_mode = &svdm_tbt_compat_response_enter_mode,
	.amode = NULL,
	.exit_mode = NULL,
};
