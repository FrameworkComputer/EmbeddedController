/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Thunderbolt alternate mode support
 * Refer to USB Type-C Cable and Connector Specification Release 2.0 Section F
 */

#include <stdbool.h>
#include <stdint.h>
#include "compile_time_macros.h"
#include "console.h"
#include "tcpm.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm.h"
#include "usb_pd_tbt.h"
#include "usb_pe_sm.h"
#include "usb_tbt_alt_mode.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

static int tbt_prints(const char *string, int port)
{
	return CPRINTS("C%d: TBT %s", port, string);
}

/* The states of Thunderbolt negotiation */
enum tbt_states {
	TBT_START = 0,
	TBT_ENTER_SOP_SENT,
	TBT_ENTER_SOP_NACKED,
	TBT_ACTIVE,
	TBT_EXIT_SOP_SENT,
	TBT_EXIT_SOP_RETRY_SENT,
	TBT_ENTER_SOP_RETRY,
	TBT_ENTER_SOP_RETRY_SENT,
	TBT_INACTIVE,
	TBT_STATE_COUNT,
};
static enum tbt_states tbt_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static const uint8_t state_vdm_cmd[TBT_STATE_COUNT] = {
	[TBT_ENTER_SOP_SENT] = CMD_ENTER_MODE,
	[TBT_ACTIVE] = CMD_EXIT_MODE,
	[TBT_EXIT_SOP_SENT] = CMD_EXIT_MODE,
	[TBT_EXIT_SOP_RETRY_SENT] = CMD_EXIT_MODE,
	[TBT_ENTER_SOP_RETRY_SENT] = CMD_ENTER_MODE,
};

void tbt_init(int port)
{
	tbt_state[port] = TBT_START;
}

void tbt_teardown(int port)
{
	 tbt_prints("teardown", port);
	 tbt_state[port] = TBT_INACTIVE;
}

static void tbt_entry_failed(int port)
{
	tbt_prints("alt mode protocol failed!", port);
	tbt_state[port] = TBT_INACTIVE;
	dpm_set_mode_entry_done(port);
}

static bool tbt_response_valid(int port, enum tcpm_transmit_type type,
				char *cmdt, int vdm_cmd)
{
	enum tbt_states st = tbt_state[port];

	/*
	 * Check for an unexpected response.
	 * If Thunderbolt is inactive, ignore the command.
	 */
	if ((st != TBT_INACTIVE && state_vdm_cmd[st] != vdm_cmd) ||
	    (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE &&
	     type != TCPC_TX_SOP)) {
		tbt_entry_failed(port);
		return false;
	}
	return true;
}

void intel_vdm_acked(int port, enum tcpm_transmit_type type, int vdo_count,
		uint32_t *vdm)
{
	const uint8_t vdm_cmd = PD_VDO_CMD(vdm[0]);

	if (!tbt_response_valid(port, type, "ACK", vdm_cmd))
		return;

	switch (tbt_state[port]) {
	case TBT_ENTER_SOP_SENT:
	case TBT_ENTER_SOP_RETRY_SENT:
		set_tbt_compat_mode_ready(port);
		dpm_set_mode_entry_done(port);
		tbt_state[port] = TBT_ACTIVE;
		tbt_prints("enter mode SOP", port);
		break;
	case TBT_EXIT_SOP_SENT:
		/*
		 * Request to exit mode successful, so put it in
		 * inactive state.
		 */
		tbt_prints("exit mode SOP", port);
		tbt_state[port] = TBT_INACTIVE;
		break;
	case TBT_EXIT_SOP_RETRY_SENT:
		/*
		 * The request to exit the mode was successful,
		 * so try to enter the mode again.
		 */
		tbt_state[port] = TBT_ENTER_SOP_RETRY;
		break;
	case TBT_INACTIVE:
		/*
		 * This can occur if the mode is shutdown because
		 * the CPU is being turned off, and an exit mode
		 * command has been sent.
		 */
		break;
	default:
		/* Invalid or unexpected negotiation state */
		 CPRINTF("%s called with invalid state %d\n",
				__func__, tbt_state[port]);
		tbt_entry_failed(port);
		break;
	}
}

void intel_vdm_naked(int port, enum tcpm_transmit_type type, uint8_t vdm_cmd)
{
	if (!tbt_response_valid(port, type, "NACK", vdm_cmd))
		return;

	switch (tbt_state[port]) {
	case TBT_ENTER_SOP_SENT:
		/*
		 * If a request to enter Thunderbolt mode is NAK'ed, this
		 * likely means the partner is already in Thunderbolt alt mode,
		 * so request to exit the mode first before retrying the enter
		 * command. This can happen if the EC is restarted
		 */
		tbt_state[port] = TBT_ENTER_SOP_NACKED;
		break;
	case TBT_ENTER_SOP_RETRY_SENT:
		/*
		 * Another NAK on the second attempt to enter Thunderbolt mode.
		 * Give up.
		 */
		tbt_entry_failed(port);
		break;
	default:
		CPRINTS("C%d: NAK for cmd %d in state %d", port,
			vdm_cmd, tbt_state[port]);
		tbt_entry_failed(port);
		break;
	}
}

int tbt_setup_next_vdm(int port, int vdo_count, uint32_t *vdm)
{
	const struct pd_discovery *disc =
			pd_get_am_discovery(port, TCPC_TX_SOP);
	struct svdm_amode_data *modep;
	int vdo_count_ret = 0;

	if (vdo_count < VDO_MAX_SIZE ||
	    !disc->identity.idh.modal_support ||
	    !is_tbt_cable_superspeed(port)) {
		return -1;
	}

	switch (tbt_state[port]) {
	case TBT_START:
	case TBT_ENTER_SOP_RETRY:
		if (tbt_state[port] == TBT_START)
			tbt_prints("attempt to enter mode", port);
		/*
		 * Note: If it's not a Passive cable, the tbt_setup_next_vdm()
		 * function will return zero
		 */
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE) {
			vdo_count_ret =
				enter_tbt_compat_mode(port, TCPC_TX_SOP, vdm);
			if (tbt_state[port] == TBT_START)
				tbt_state[port] = TBT_ENTER_SOP_SENT;
			else
				tbt_state[port] = TBT_ENTER_SOP_RETRY_SENT;
			break;
		}
		/*
		 * TODO(b/148528713): Add support for Thunderbolt active cable.
		 */
	case TBT_ENTER_SOP_NACKED:
	case TBT_ACTIVE:
		/*
		 * Called to exit Thunderbolt alt mode, either when the mode is
		 * active and the system is shutting down, or when an initial
		 * request to enter the mode is NAK'ed. This can happen if EC
		 * is restarted while Thunderbolt mode is active.
		 */
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE) {
			modep = pd_get_amode_data(port,
						  TCPC_TX_SOP, USB_VID_INTEL);
			if (!(modep && modep->opos))
				return -1;

			vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE) |
				 VDO_OPOS(modep->opos) |
				 VDO_CMDT(CMDT_INIT) |
				 VDO_SVDM_VERS(
					pd_get_vdo_ver(port, TCPC_TX_SOP));
			vdo_count_ret = 1;
			tbt_state[port] = (tbt_state[port] == TBT_ACTIVE) ?
					   TBT_EXIT_SOP_SENT :
					   TBT_EXIT_SOP_RETRY_SENT;
		}
		break;
	case TBT_INACTIVE:
		/* Thunderbolt mode is inactive */
		return -1;
	default:
		 CPRINTF("%s called with invalid state %d\n",
				__func__, tbt_state[port]);
		return -1;
	}

	return vdo_count_ret;
}
