/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "cros_cbi.h"
#include "driver/ppc/ktu1125_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "hooks.h"
#include "ppc/syv682x_public.h"
#include "system.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_mux_config.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_vdo.h"
#include "usbc_config.h"
#include "usbc_ppc.h"
#include "util.h"

#include <stdbool.h>

#include <zephyr/drivers/espi.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* eSPI device */
#define espi_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_espi))

void board_reset_pd_mcu(void)
{
	/* Nothing to do */
}

__override bool board_is_tbt_usb4_port(int port)
{
	return true;
}

static void usbc_interrupt_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		board_reset_pd_mcu();
	}
}
DECLARE_HOOK(HOOK_INIT, usbc_interrupt_init, HOOK_PRIO_POST_I2C);

__override void board_overcurrent_event(int port, int is_overcurrented)
{
	/*
	 * Meteorlake PCH uses Virtual Wire for over current error,
	 * hence Send Over Current Virtual Wire' eSPI signal.
	 */
	espi_send_vwire(espi_dev, port + ESPI_VWIRE_SIGNAL_TARGET_GPIO_0,
			!is_overcurrented);
}

static void board_disable_charger_ports(void)
{
	int i;

	CPRINTSUSB("Disabling all charger ports");

	/* Disable all ports. */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		/*
		 * Do not return early if one fails otherwise we can
		 * get into a boot loop assertion failure.
		 */
		if (ppc_vbus_sink_enable(i, 0)) {
			CPRINTSUSB("Disabling C%d as sink failed.", i);
		}
	}
}

int board_set_active_charge_port(int port)
{
	bool is_valid_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (port == CHARGE_PORT_NONE) {
		board_disable_charger_ports();
		return EC_SUCCESS;
	} else if (!is_valid_port) {
		return EC_ERROR_INVAL;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTSUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port) {
			continue;
		}
		if (ppc_vbus_sink_enable(i, 0)) {
			CPRINTSUSB("C%d: sink path disable failed.", i);
		}
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTSUSB("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
/* ----------------- Vendor Defined Messages ------------------ */
/* Responses specifically for the enablement of TBT mode in the role of UFP */

#define OPOS_TBT 1

static const union tbt_mode_resp_device vdo_tbt_modes[1] = { {
	.tbt_alt_mode = 0x0001,
	.tbt_adapter = TBT_ADAPTER_TBT3,
	.intel_spec_b0 = 0,
	.vendor_spec_b0 = 0,
	.vendor_spec_b1 = 0,
} };

static const uint32_t vdo_idh_no_ufp = VDO_IDH(1, /* Data caps as USB host */
					       0, /* Not a USB device */
					       IDH_PTYPE_UNDEF, 0, /* No UFP
								    * modal
								    * operation
								    */
					       USB_VID_GOOGLE);

static const uint32_t vdo_idh_tbt = VDO_IDH(1, /* Data caps as USB host */
					    0, /* Not a USB device */
					    IDH_PTYPE_PERIPH, 1, /* UFP modal
								  * operation
								  */
					    USB_VID_GOOGLE);

static const uint32_t vdo_idh_rev30_no_ufp =
	VDO_IDH_REV30(1, /* Data caps as USB host */
		      0, /* Not a USB device */
		      IDH_PTYPE_UNDEF, 0, /* No UFP modal operation */
		      IDH_PTYPE_DFP_HOST, USB_TYPEC_RECEPTACLE, USB_VID_GOOGLE);

static const uint32_t vdo_idh_rev30_tbt =
	VDO_IDH_REV30(1, /* Data caps as USB host */
		      0, /* Not a USB device */
		      IDH_PTYPE_PERIPH, 1, /* UFP modal operation */
		      IDH_PTYPE_DFP_HOST, USB_TYPEC_RECEPTACLE, USB_VID_GOOGLE);

static const uint32_t vdo_product =
	VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

static const uint32_t vdo_ufp1 = VDO_UFP1(
	(VDO_UFP1_CAPABILITY_USB20 | VDO_UFP1_CAPABILITY_USB32),
	USB_TYPEC_RECEPTACLE, VDO_UFP1_ALT_MODE_TBT3, USB_R30_SS_U40_GEN3);

static const uint32_t vdo_dfp =
	VDO_DFP((VDO_DFP_HOST_CAPABILITY_USB20 | VDO_DFP_HOST_CAPABILITY_USB32 |
		 VDO_DFP_HOST_CAPABILITY_USB4),
		USB_TYPEC_RECEPTACLE, 1 /* Port 1 */);

/* Track whether we've been enabled to ACK TBT EnterModes requests */
static bool tbt_ufp_ack_allowed[CONFIG_USB_PD_PORT_MAX_COUNT];

static int svdm_tbt_compat_response_identity(int port, uint32_t *payload)
{
	/*
	 * For PD 3.1 compliance test TEST.PD.VDM.SRC.2,
	 * we should return NAK if we cannot recognized the incoming SVID.
	 */
	if (PD_VDO_VID(payload[0]) == USB_SID_PD) {
		/* TODO(b/154962766): Get an XID */
		payload[VDO_I(CSTAT)] = VDO_CSTAT(0);
		payload[VDO_I(PRODUCT)] = vdo_product;

		if (pd_get_rev(port, TCPCI_MSG_SOP) == PD_REV30) {
			/* PD Revision 3.0 */
			if (tbt_ufp_ack_allowed[port]) {
				payload[VDO_I(IDH)] = vdo_idh_rev30_tbt;
				payload[VDO_I(PTYPE_UFP1_VDO)] = vdo_ufp1;
			} else {
				payload[VDO_I(IDH)] = vdo_idh_rev30_no_ufp;
				payload[VDO_I(PTYPE_UFP1_VDO)] = 0;
			}
			payload[VDO_I(PTYPE_UFP2_VDO)] = 0;
			payload[VDO_I(PTYPE_DFP_VDO)] = vdo_dfp;
			return VDO_I(PTYPE_DFP_VDO) + 1;
		}

		/* PD Revision 2.0 */
		if (tbt_ufp_ack_allowed[port]) {
			payload[VDO_I(IDH)] = vdo_idh_tbt;
		} else {
			payload[VDO_I(IDH)] = vdo_idh_no_ufp;
		}
		return VDO_I(PRODUCT) + 1;
	} else {
		return 0; /* NAK */
	}
}

static int svdm_tbt_compat_response_svids(int port, uint32_t *payload)
{
	/*
	 * For PD 3.1 compliance test TEST.PD.VDM.SRC.2,
	 * we should return NAK if we cannot recognized the incoming SVID.
	 */
	if (PD_VDO_VID(payload[0]) == USB_SID_PD && tbt_ufp_ack_allowed[port]) {
		payload[1] = VDO_SVID(USB_VID_INTEL, 0);
		return 2;
	} else {
		return 0; /* NAK */
	}
}

static int svdm_tbt_compat_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) == USB_VID_INTEL &&
	    tbt_ufp_ack_allowed[port]) {
		memcpy(payload + 1, vdo_tbt_modes, sizeof(vdo_tbt_modes));
		return ARRAY_SIZE(vdo_tbt_modes) + 1;
	} else {
		return 0; /* NAK */
	}
}

__override enum ec_status
board_set_tbt_ufp_reply(int port, enum typec_tbt_ufp_reply reply)
{
	/* Note: Host command has already bounds-checked port */
	if (reply == TYPEC_TBT_UFP_REPLY_ACK)
		tbt_ufp_ack_allowed[port] = true;
	else if (reply == TYPEC_TBT_UFP_REPLY_NAK)
		tbt_ufp_ack_allowed[port] = false;
	else
		return EC_RES_INVALID_PARAM;

	return EC_RES_SUCCESS;
}

static int svdm_tbt_compat_response_enter_mode(int port, uint32_t *payload)
{
	mux_state_t mux_state = 0;

	/* Do not enter mode while CPU is off. */
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF))
		return 0; /* NAK */

	/* Do not enter mode while policy disallows it */
	if (!tbt_ufp_ack_allowed[port])
		return 0; /* NAK */

	if ((PD_VDO_VID(payload[0]) != USB_VID_INTEL) ||
	    (PD_VDO_OPOS(payload[0]) != OPOS_TBT))
		return 0; /* NAK */

	mux_state = usb_mux_get(port);
	/*
	 * Ref: USB PD 3.0 Spec figure 6-21 Successful Enter Mode sequence
	 * UFP (responder) should be in USB mode or safe mode before entering a
	 * Mode that requires the reconfiguring of any pins.
	 */
	if ((mux_state & USB_PD_MUX_USB_ENABLED) ||
	    (mux_state & USB_PD_MUX_SAFE_MODE)) {
		pd_ufp_set_enter_mode(port, payload);
		set_tbt_compat_mode_ready(port);

		/*
		 * Ref: Above figure 6-21: UFP (responder) should be in the new
		 * mode before sending the ACK.  However, our mux set sequence
		 * may exceed tVDMEnterMode, so wait as long as we can
		 * before sending the reply without violating that timer.
		 */
		if (!usb_mux_set_completed(port))
			k_usleep(PD_T_VDM_E_MODE / 2);

		CPRINTFUSB("UFP Enter TBT mode");
		return 1; /* ACK */
	}

	CPRINTFUSB("UFP failed to enter TBT mode(mux=0x%x)", mux_state);
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
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */
