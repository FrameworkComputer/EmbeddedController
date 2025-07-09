/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * SVDM identity support for DFP-only devices.
 *
 * This file is only supported for Zephyr builds, enabled by
 * CONFIG_SVDM_RSP_DFP_ONLY. No equivalent config exists for EC-OS.
 */

#include "usb_pd.h"

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

__override const struct svdm_response svdm_rsp = {
	.identity = svdm_identity,
	/*
	 * Discover Identity support is required for devices with more than one
	 * DFP, but other SVDM commands are optional. We don't support operating
	 * as Responder in any mode, so leave them unimplemented. See 6.13.5,
	 * Applicability of Structured VDM Commands.
	 */
};
