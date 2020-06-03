/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Device Policy Manager implementation
 * Refer to USB PD 3.0 spec, version 2.0, sections 8.2 and 8.3
 */

#include "compile_time_macros.h"
#include "console.h"
#include "usb_dp_alt_mode.h"
#include "usb_pd.h"
#include "usb_pd_dpm.h"
#include "usb_pe_sm.h"
#include "tcpm.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

static struct {
	bool mode_entry_done;
} dpm[CONFIG_USB_PD_PORT_MAX_COUNT];

void dpm_init(int port)
{
	dpm[port].mode_entry_done = false;
}

void dpm_set_mode_entry_done(int port)
{
	dpm[port].mode_entry_done = true;
}

void dpm_vdm_acked(int port, enum tcpm_transmit_type type, int vdo_count,
		uint32_t *vdm)
{
	const uint16_t svid = PD_VDO_VID(vdm[0]);

	assert(vdo_count >= 1);

	switch (svid) {
	case USB_SID_DISPLAYPORT:
		dp_vdm_acked(port, type, vdo_count, vdm);
		break;
	default:
		CPRINTS("C%d: Received unexpected VDM ACK for SVID %d", port,
				svid);
	}
}

void dpm_vdm_naked(int port, enum tcpm_transmit_type type, uint16_t svid,
		uint8_t vdm_cmd)
{
	switch (svid) {
	case USB_SID_DISPLAYPORT:
		dp_vdm_naked(port, type, vdm_cmd);
		break;
	default:
		CPRINTS("C%d: Received unexpected VDM NAK for SVID %d", port,
				svid);
	}
}

void dpm_attempt_mode_entry(int port)
{
	uint32_t vdo_count;
	uint32_t vdm[VDO_MAX_SIZE];

	if (dpm[port].mode_entry_done)
		return;

	if (pd_get_data_role(port) != PD_ROLE_DFP)
		return;

	/*
	 * Check if we even discovered a DisplayPort mode; if not, just
	 * mark discovery done and get out of here.
	 */
	if (!pd_is_mode_discovered_for_svid(port, TCPC_TX_SOP,
				USB_SID_DISPLAYPORT)) {
		CPRINTF("C%d: No DP mode discovered\n", port);
		dpm_set_mode_entry_done(port);
		return;
	}

	vdo_count = dp_setup_next_vdm(port, ARRAY_SIZE(vdm), vdm);
	if (vdo_count < 0) {
		dpm_set_mode_entry_done(port);
		CPRINTF("C%d: Couldn't set up DP VDM\n", port);
		return;
	}

	/*
	 * TODO(b/155890173): Provide a host command to request that the PE send
	 * an arbitrary VDM via this mechanism.
	 */
	if (!pd_setup_vdm_request(port, TCPC_TX_SOP, vdm, vdo_count)) {
		dpm_set_mode_entry_done(port);
		return;
	}

	pe_dpm_request(port, DPM_REQUEST_VDM);
}
