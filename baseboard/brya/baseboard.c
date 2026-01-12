/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "tablet_mode.h"
#include "throttle_ap.h"
#include "usb_pd.h"

/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_ACOK_OD,
	GPIO_GSC_EC_PWR_BTN_ODL,
	GPIO_LID_OPEN,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

#ifndef HAS_TASK_PROCHOT
static const struct prochot_cfg brya_prochot_cfg = {
	.gpio_prochot_in = GPIO_EC_PROCHOT_IN_L,
};

static void prochot_monitoring_init(void)
{
	/* Enable monitoring of the PROCHOT input to the EC */
	throttle_ap_config_prochot(&brya_prochot_cfg);
	gpio_enable_interrupt(GPIO_EC_PROCHOT_IN_L);
}
DECLARE_HOOK(HOOK_INIT, prochot_monitoring_init, HOOK_PRIO_DEFAULT);

static int svdm_identity(int port, uint32_t *payload)
{
	/* The SVID in the Discover Identity Command request Shall be set to the
	 * PD SID */
	if (PD_VDO_VID(payload[VDO_INDEX_HDR]) != USB_SID_PD) {
		return 0;
	}

	payload[VDO_I(CSTAT)] = VDO_CSTAT(CONFIG_USB_PD_XID);
	payload[VDO_I(PRODUCT)] =
		VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

	if (pd_get_rev(port, TCPCI_MSG_SOP) < PD_REV30) {
		payload[VDO_I(IDH)] = VDO_IDH(1, /* USB host */
					      0, /* Not a USB device */
					      IDH_PTYPE_UNDEF, /* Not a UFP */
					      0, /* No alt modes (not a UFP) */
					      CONFIG_USB_VID);

		return VDO_I(PRODUCT) + 1;
	} else {
		payload[VDO_I(IDH)] =
			VDO_IDH_REV30(1, /* USB host */
				      0, /* Not a USB device */
				      IDH_PTYPE_UNDEF, /* Not a UFP */
				      0, /* No alt modes (not a UFP) */
				      IDH_PTYPE_DFP_HOST, /* PDUSB host */
				      USB_TYPEC_RECEPTACLE, CONFIG_USB_VID);

		/* Single VDO for DFP product type */
		payload[VDO_I(PRODUCT) + 1] =
			VDO_DFP(VDO_DFP_HOST_CAPABILITY_USB32,
				USB_TYPEC_RECEPTACLE, port);

		return VDO_I(PRODUCT) + 2;
	}
}

/* 6.4.4.3.2 A Responder that does not support any SVIDs Shall return a NAK.*/
static int svdm_svids(int port, uint32_t *payload)
{
	return 0;
}

__override const struct svdm_response svdm_rsp = {
	.identity = svdm_identity,
	.svids = svdm_svids,
	/*
	 * Discover Identity support is required for devices with more than one
	 * DFP, but other SVDM commands are optional. We don't support operating
	 * as Responder in any mode, so leave them unimplemented. See 6.13.5,
	 * Applicability of Structured VDM Commands.
	 */
};
#endif /* !HAS_TASK_PROCHOT */
