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

/*
 * Enter/Exit TBT mode with active cable
 *
 *
 *                      TBT_START                           |------------
 *                 retry_done = false                       |           |
 *                           |                              v           |
 *                           |<------------------|    Exit Mode SOP     |
 *                           | retry_done = true |          |           |
 *                           v                   |          | ACK/NAK   |
 *                    Enter Mode SOP'            |  --------|---------  |
 *                       ACK | NAK               |    Exit Mode SOP''   |
 *                    |------|------|            |          |           |
 *                    |             |            |          | ACK/NAK   |
 *                    v             |            |  --------|---------  |
 *             Enter Mode SOP''     |            |     Exit Mode SOP'   |
 *                    |             |            |          |           |
 *                ACK | NAK         |            |          | ACK/NAK   |
 *             |------|------|      |            |  ------------------  |
 *             |             |      |            | retry_done == true?  |
 *             v             |      |            |          |           |
 *       Enter Mode SOP      |      |            |   No     |           |
 *             |             |      |            |-----------           |
 *         ACK | NAK         |      |                       |Yes        |
 *     |-------|------|      |      |                       v           |
 *     |              |      |      |                  TBT_INACTIVE     |
 *     v              |      |      |              retry_done = false   |
 * TBT_ACTIVE         |      |      |                                   |
 * retry_done = true  |      |      |                                   |
 *     |              |      |      |                                   |
 *     v              v      v      v                                   |
 *     -----------------------------------------------------------------|
 */

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/*
 * If a partner sends an Enter Mode NAK, Exit Mode and try again. This has
 * happened when the EC loses state after previously entering an alt mode
 * with a partner. It may be fixed in b/159495742, in which case this
 * logic is unneeded.
 */
static bool retry_done;

static int tbt_prints(const char *string, int port)
{
	return CPRINTS("C%d: TBT %s", port, string);
}

/* The states of Thunderbolt negotiation */
enum tbt_states {
	TBT_START = 0,
	TBT_ENTER_SOP,
	TBT_ACTIVE,
	TBT_EXIT_SOP,
	TBT_INACTIVE,
	/* Active cable only */
	TBT_ENTER_SOP_PRIME,
	TBT_ENTER_SOP_PRIME_PRIME,
	TBT_EXIT_SOP_PRIME,
	TBT_EXIT_SOP_PRIME_PRIME,
	TBT_STATE_COUNT,
};
static enum tbt_states tbt_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static const uint8_t state_vdm_cmd[TBT_STATE_COUNT] = {
	[TBT_ENTER_SOP] = CMD_ENTER_MODE,
	[TBT_ACTIVE] = CMD_EXIT_MODE,
	[TBT_EXIT_SOP] = CMD_EXIT_MODE,
	/* Active cable only */
	[TBT_ENTER_SOP_PRIME] = CMD_ENTER_MODE,
	[TBT_ENTER_SOP_PRIME_PRIME] = CMD_ENTER_MODE,
	[TBT_EXIT_SOP_PRIME] = CMD_EXIT_MODE,
	[TBT_EXIT_SOP_PRIME_PRIME] = CMD_EXIT_MODE,
};

void tbt_init(int port)
{
	tbt_state[port] = TBT_START;
	retry_done = false;
}

bool tbt_is_active(int port)
{
	return tbt_state[port] == TBT_ACTIVE;
}

void tbt_teardown(int port)
{
	 tbt_prints("teardown", port);
	 tbt_state[port] = TBT_INACTIVE;
	 retry_done = false;
}

static void tbt_entry_failed(int port)
{
	tbt_prints("alt mode protocol failed!", port);
	tbt_state[port] = TBT_INACTIVE;
	retry_done = false;
	dpm_set_mode_entry_done(port);
}

static bool tbt_response_valid(int port, enum tcpm_transmit_type type,
				char *cmdt, int vdm_cmd)
{
	enum tbt_states st = tbt_state[port];

	/*
	 * Check for an unexpected response.
	 * 1. invalid command
	 * 2. invalid Tx type for passive cable
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

/* Exit Mode process is complete, but retry Enter Mode process */
static void tbt_retry_enter_mode(int port)
{
	tbt_state[port] = TBT_START;
	retry_done = true;
}

/* Send Exit Mode to SOP''(if supported), or SOP' */
static void tbt_active_cable_exit_mode(int port)
{
	struct pd_discovery *disc;

	disc = pd_get_am_discovery(port, TCPC_TX_SOP_PRIME);

	if (disc->identity.product_t1.a_rev20.sop_p_p)
		tbt_state[port] = TBT_EXIT_SOP_PRIME_PRIME;
	else
		tbt_state[port] = TBT_EXIT_SOP_PRIME;
}

void intel_vdm_acked(int port, enum tcpm_transmit_type type, int vdo_count,
		uint32_t *vdm)
{
	struct pd_discovery *disc;
	const uint8_t vdm_cmd = PD_VDO_CMD(vdm[0]);

	if (!tbt_response_valid(port, type, "ACK", vdm_cmd))
		return;

	disc = pd_get_am_discovery(port, TCPC_TX_SOP_PRIME);

	switch (tbt_state[port]) {
	case TBT_ENTER_SOP_PRIME:
		if (disc->identity.product_t1.a_rev20.sop_p_p)
			tbt_state[port] = TBT_ENTER_SOP_PRIME_PRIME;
		else
			tbt_state[port] = TBT_ENTER_SOP;
		break;
	case TBT_ENTER_SOP_PRIME_PRIME:
		tbt_state[port] = TBT_ENTER_SOP;
		break;
	case TBT_ENTER_SOP:
		set_tbt_compat_mode_ready(port);
		dpm_set_mode_entry_done(port);
		tbt_state[port] = TBT_ACTIVE;
		retry_done = true;
		tbt_prints("enter mode SOP", port);
		break;
	case TBT_ACTIVE:
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE)
			tbt_active_cable_exit_mode(port);
		else {
			/*
			 * Exit Mode process is complete; go to inactive state.
			 */
			tbt_prints("exit mode SOP", port);
			tbt_state[port] = TBT_INACTIVE;
			retry_done = false;
		}
		break;
	case TBT_EXIT_SOP:
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE)
			tbt_active_cable_exit_mode(port);
		else {
			if (retry_done)
				/* retried enter mode, still failed, give up */
				tbt_entry_failed(port);
			else
				tbt_retry_enter_mode(port);
		}
		break;
	case TBT_EXIT_SOP_PRIME_PRIME:
		tbt_state[port] = TBT_EXIT_SOP_PRIME;
		break;
	case TBT_EXIT_SOP_PRIME:
		if (retry_done) {
			/*
			 * Exit mode process is complete; go to inactive state.
			 */
			tbt_prints("exit mode SOP'", port);
			tbt_entry_failed(port);
		} else {
			tbt_retry_enter_mode(port);
		}
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
	if (!tbt_response_valid(port, type, "NAK", vdm_cmd))
		return;

	switch (tbt_state[port]) {
	case TBT_ENTER_SOP_PRIME:
	case TBT_ENTER_SOP_PRIME_PRIME:
	case TBT_ENTER_SOP:
		/*
		 * If a request to enter Thunderbolt mode is NAK'ed, this
		 * likely means the partner is already in Thunderbolt alt mode,
		 * so request to exit the mode first before retrying the enter
		 * command. This can happen if the EC is restarted
		 */
		tbt_state[port] = TBT_EXIT_SOP;
		break;
	case TBT_ACTIVE:
		/* Exit SOP got NAK'ed */
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE)
			tbt_active_cable_exit_mode(port);
		else {
			tbt_prints("exit mode SOP failed", port);
			tbt_state[port] = TBT_INACTIVE;
			retry_done = false;
		}
		break;
	case TBT_EXIT_SOP:
		/* Exit SOP got NAK'ed */
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE)
			tbt_active_cable_exit_mode(port);
		else {
			if (retry_done)
				/* Retried enter mode, still failed, give up */
				tbt_entry_failed(port);
			else
				tbt_retry_enter_mode(port);
		}
		break;
	case TBT_EXIT_SOP_PRIME_PRIME:
		tbt_prints("exit mode SOP'' failed", port);
		tbt_state[port] = TBT_EXIT_SOP_PRIME;
		break;
	case TBT_EXIT_SOP_PRIME:
		if (retry_done) {
			/*
			 * Exit mode process is complete; go to inactive state.
			 */
			tbt_prints("exit mode SOP' failed", port);
			tbt_entry_failed(port);
		} else {
			tbt_retry_enter_mode(port);
		}
		break;
	default:
		CPRINTS("C%d: NAK for cmd %d in state %d", port,
			vdm_cmd, tbt_state[port]);
		tbt_entry_failed(port);
		break;
	}
}

static bool tbt_mode_is_supported(int port, int vdo_count)
{
	const struct pd_discovery *disc =
			pd_get_am_discovery(port, TCPC_TX_SOP);

	return disc->identity.idh.modal_support &&
		is_tbt_cable_superspeed(port) &&
		get_tbt_cable_speed(port) >= TBT_SS_U31_GEN1;
}

int tbt_setup_next_vdm(int port, int vdo_count, uint32_t *vdm,
		enum tcpm_transmit_type *tx_type)
{
	struct svdm_amode_data *modep;
	int vdo_count_ret = 0;

	*tx_type = TCPC_TX_SOP;

	if (vdo_count < VDO_MAX_SIZE)
		return -1;

	switch (tbt_state[port]) {
	case TBT_START:
		if (!tbt_mode_is_supported(port, vdo_count))
			return 0;

		if (!retry_done)
			tbt_prints("attempt to enter mode", port);
		else
			tbt_prints("retry to enter mode", port);
		/* Active cable send Enter Mode SOP' first */
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE) {
			vdo_count_ret =
				enter_tbt_compat_mode(
					port, TCPC_TX_SOP_PRIME, vdm);
			*tx_type = TCPC_TX_SOP_PRIME;
			tbt_state[port] = TBT_ENTER_SOP_PRIME;
		} else {
			/* Passive cable send Enter Mode SOP */
			vdo_count_ret =
				enter_tbt_compat_mode(port, TCPC_TX_SOP, vdm);
			tbt_state[port] = TBT_ENTER_SOP;
		}
		break;
	case TBT_ENTER_SOP_PRIME:
		vdo_count_ret =
			enter_tbt_compat_mode(port, TCPC_TX_SOP_PRIME, vdm);
		*tx_type = TCPC_TX_SOP_PRIME;
		break;
	case TBT_ENTER_SOP_PRIME_PRIME:
		vdo_count_ret =
			enter_tbt_compat_mode(
				port, TCPC_TX_SOP_PRIME_PRIME, vdm);
		*tx_type = TCPC_TX_SOP_PRIME_PRIME;
		break;
	case TBT_ENTER_SOP:
		vdo_count_ret =
			enter_tbt_compat_mode(port, TCPC_TX_SOP, vdm);
		break;
	case TBT_EXIT_SOP:
	case TBT_ACTIVE:
		/*
		 * Called to exit Thunderbolt alt mode, either when the mode is
		 * active and the system is shutting down, or when an initial
		 * request to enter the mode is NAK'ed. This can happen if EC
		 * is restarted while Thunderbolt mode is active.
		 */
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
		break;
	case TBT_EXIT_SOP_PRIME_PRIME:
		modep = pd_get_amode_data(port,
			TCPC_TX_SOP_PRIME, USB_VID_INTEL);
		if (!(modep && modep->opos))
			return -1;

		vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE) |
			VDO_OPOS(modep->opos) |
			VDO_CMDT(CMDT_INIT) |
			VDO_SVDM_VERS(pd_get_vdo_ver(port,
				TCPC_TX_SOP_PRIME_PRIME));
		vdo_count_ret = 1;
		*tx_type = TCPC_TX_SOP_PRIME_PRIME;
		break;
	case TBT_EXIT_SOP_PRIME:
		modep = pd_get_amode_data(port,
				TCPC_TX_SOP_PRIME, USB_VID_INTEL);
		if (!(modep && modep->opos))
			return -1;

		vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE) |
			VDO_OPOS(modep->opos) |
			VDO_CMDT(CMDT_INIT) |
			VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP_PRIME));
		vdo_count_ret = 1;
		*tx_type = TCPC_TX_SOP_PRIME;
		break;
	case TBT_INACTIVE:
		/* Thunderbolt mode is inactive */
		return 0;
	default:
		 CPRINTF("%s called with invalid state %d\n",
				__func__, tbt_state[port]);
		return -1;
	}

	return vdo_count_ret;
}
