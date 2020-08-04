/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * DisplayPort alternate mode support
 * Refer to VESA DisplayPort Alt Mode on USB Type-C Standard, version 2.0,
 * section 5.2
 */

#include <stdbool.h>
#include <stdint.h>
#include "assert.h"
#include "usb_pd.h"
#include "usb_dp_alt_mode.h"
#include "usb_pd_dpm.h"
#include "usb_pd_tcpm.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* The state of the DP negotiation */
enum dp_states {
	DP_START = 0,
	DP_ENTER_ACKED,
	DP_ENTER_NAKED,
	DP_STATUS_ACKED,
	DP_ACTIVE,
	DP_ENTER_RETRY,
	DP_INACTIVE,
	DP_STATE_COUNT
};
static enum dp_states dp_state[CONFIG_USB_PD_PORT_MAX_COUNT];

/*
 * Map of states to expected VDM commands in responses.
 * Default of 0 indicates no command expected.
 */
static const uint8_t state_vdm_cmd[DP_STATE_COUNT] = {
	[DP_START] = CMD_ENTER_MODE,
	[DP_ENTER_ACKED] = CMD_DP_STATUS,
	[DP_STATUS_ACKED] = CMD_DP_CONFIG,
	[DP_ACTIVE] = CMD_EXIT_MODE,
	[DP_ENTER_NAKED] = CMD_EXIT_MODE,
	[DP_ENTER_RETRY] = CMD_ENTER_MODE,
};

bool dp_is_active(int port)
{
	return dp_state[port] == DP_ACTIVE;
}

void dp_init(int port)
{
	dp_state[port] = DP_START;
}

void dp_teardown(int port)
{
	CPRINTS("C%d: DP teardown", port);
	dp_state[port] = DP_INACTIVE;
}

static void dp_entry_failed(int port)
{
	CPRINTS("C%d: DP alt mode protocol failed!", port);
	dp_state[port] = DP_INACTIVE;
	dpm_set_mode_entry_done(port);
}

static bool dp_response_valid(int port, enum tcpm_transmit_type type,
			     char *cmdt, int vdm_cmd)
{
	enum dp_states st = dp_state[port];

	/*
	 * Check for an unexpected response.
	 * If DP is inactive, ignore the command.
	 */
	if (type != TCPC_TX_SOP ||
	    (st != DP_INACTIVE && state_vdm_cmd[st] != vdm_cmd)) {
		CPRINTS("C%d: Received unexpected DP VDM %s (cmd %d) from"
			" %s in state %d", port, cmdt, vdm_cmd,
			type == TCPC_TX_SOP ? "port partner" : "cable plug",
			st);
		dp_entry_failed(port);
		return false;
	}
	return true;
}

void dp_vdm_acked(int port, enum tcpm_transmit_type type, int vdo_count,
		uint32_t *vdm)
{
	const struct svdm_amode_data *modep =
		pd_get_amode_data(port, type, USB_SID_DISPLAYPORT);
	const uint8_t vdm_cmd = PD_VDO_CMD(vdm[0]);

	if (!dp_response_valid(port, type, "ACK", vdm_cmd))
		return;

	/* TODO(b/155890173): Validate VDO count for specific commands */

	switch (dp_state[port]) {
	case DP_START:
	case DP_ENTER_RETRY:
		dp_state[port] = DP_ENTER_ACKED;
		break;
	case DP_ENTER_ACKED:
		/* DP status response & UFP's DP attention have same payload. */
		dfp_consume_attention(port, vdm);
		dp_state[port] = DP_STATUS_ACKED;
		break;
	case DP_STATUS_ACKED:
		if (modep && modep->opos && modep->fx->post_config)
			modep->fx->post_config(port);
		dpm_set_mode_entry_done(port);
		dp_state[port] = DP_ACTIVE;
		CPRINTS("C%d: Entered DP mode", port);
		break;
	case DP_ACTIVE:
		/*
		 * Request to exit mode successful, so put it in
		 * inactive state.
		 */
		CPRINTS("C%d: Exited DP mode", port);
		dp_state[port] = DP_INACTIVE;
		break;
	case DP_ENTER_NAKED:
		/*
		 * The request to exit the mode was successful,
		 * so try to enter the mode again.
		 */
		dp_state[port] = DP_ENTER_RETRY;
		break;
	case DP_INACTIVE:
		/*
		 * This can occur if the mode is shutdown because
		 * the CPU is being turned off, and an exit mode
		 * command has been sent.
		 */
		break;
	default:
		/* Invalid or unexpected negotiation state */
		CPRINTF("%s called with invalid state %d\n",
				__func__, dp_state[port]);
		dp_entry_failed(port);
		break;
	}
}

void dp_vdm_naked(int port, enum tcpm_transmit_type type, uint8_t vdm_cmd)
{
	if (!dp_response_valid(port, type, "NAK", vdm_cmd))
		return;

	switch (dp_state[port]) {
	case DP_START:
		/*
		 * If a request to enter DP mode is NAK'ed, this likely
		 * means the partner is already in DP alt mode, so
		 * request to exit the mode first before retrying
		 * the enter command. This can happen if the EC
		 * is restarted (e.g to go into recovery mode) while
		 * DP alt mode is active.
		 */
		dp_state[port] = DP_ENTER_NAKED;
		break;
	case DP_ENTER_RETRY:
		/*
		 * Another NAK on the second attempt to enter DP mode.
		 * Give up.
		 */
		dp_entry_failed(port);
		break;
	default:
		CPRINTS("C%d: NAK for cmd %d in state %d", port,
			vdm_cmd, dp_state[port]);
		dp_entry_failed(port);
		break;
	}
}

int dp_setup_next_vdm(int port, int vdo_count, uint32_t *vdm)
{
	const struct svdm_amode_data *modep = pd_get_amode_data(port,
			TCPC_TX_SOP, USB_SID_DISPLAYPORT);
	int vdo_count_ret;

	if (vdo_count < VDO_MAX_SIZE)
		return -1;

	switch (dp_state[port]) {
	case DP_START:
	case DP_ENTER_RETRY:
		/* Enter the first supported mode for DisplayPort. */
		vdm[0] = pd_dfp_enter_mode(port, TCPC_TX_SOP,
				USB_SID_DISPLAYPORT, 0);
		if (vdm[0] == 0)
			return -1;
		/* CMDT_INIT is 0, so this is a no-op */
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
		vdo_count_ret = 1;
		if (dp_state[port] == DP_START)
			CPRINTS("C%d: Attempting to enter DP mode", port);
		break;
	case DP_ENTER_ACKED:
		if (!(modep && modep->opos))
			return -1;

		vdo_count_ret = modep->fx->status(port, vdm);
		if (vdo_count_ret == 0)
			return -1;
		vdm[0] |= PD_VDO_OPOS(modep->opos);
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
		break;
	case DP_STATUS_ACKED:
		if (!(modep && modep->opos))
			return -1;

		vdo_count_ret = modep->fx->config(port, vdm);
		if (vdo_count_ret == 0)
			return -1;
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
		break;
	case DP_ENTER_NAKED:
	case DP_ACTIVE:
		/*
		 * Called to exit DP alt mode, either when the mode
		 * is active and the system is shutting down, or
		 * when an initial request to enter the mode is NAK'ed.
		 * This can happen if the EC is restarted (e.g to go
		 * into recovery mode) while DP alt mode is active.
		 * It would be good to invoke modep->fx->exit but
		 * this doesn't set up the VDM, it clears state.
		 * TODO(b/159856063): Clean up the API to the fx functions.
		 */
		if (!(modep && modep->opos))
			return -1;

		vdm[0] = VDO(USB_SID_DISPLAYPORT,
			     1, /* structured */
			     CMD_EXIT_MODE);

		vdm[0] |= VDO_OPOS(modep->opos);
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
		vdo_count_ret = 1;
		break;
	case DP_INACTIVE:
		/*
		 * DP mode is inactive.
		 */
		return -1;
	default:
		CPRINTF("%s called with invalid state %d\n",
				__func__, dp_state[port]);
		return -1;
	}
	return vdo_count_ret;
}
