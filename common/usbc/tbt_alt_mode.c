/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Thunderbolt alternate mode support
 * Refer to USB Type-C Cable and Connector Specification Release 2.0 Section F
 */

#include "atomic.h"
#include "compile_time_macros.h"
#include "console.h"
#include "tcpm/tcpm.h"
#include "typec_control.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_tbt_alt_mode.h"

#include <stdbool.h>
#include <stdint.h>

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
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
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
#define TBT_FLAG_RETRY_DONE BIT(0)
#define TBT_FLAG_EXIT_DONE BIT(1)
#define TBT_FLAG_CABLE_ENTRY_DONE BIT(2)

static uint8_t tbt_flags[CONFIG_USB_PD_PORT_MAX_COUNT];

#define TBT_SET_FLAG(port, flag) (tbt_flags[port] |= (flag))
#define TBT_CLR_FLAG(port, flag) (tbt_flags[port] &= (~flag))
#define TBT_CHK_FLAG(port, flag) (tbt_flags[port] & (flag))

/* Note: there is currently only one defined TBT mode */
static const int tbt_opos = 1;

static int tbt_prints(const char *string, int port)
{
	CPRINTS("C%d: TBT %s", port, string);
	return 0;
}

/* The states of Thunderbolt negotiation */
enum tbt_states {
	TBT_START = 0,
	TBT_ENTER_SOP,
	TBT_ACTIVE,
	/* Set to force Exit mode from non-Active states */
	TBT_PREPARE_EXIT_MODE,
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
	TBT_CLR_FLAG(port, TBT_FLAG_RETRY_DONE);
	TBT_SET_FLAG(port, TBT_FLAG_EXIT_DONE);
	TBT_CLR_FLAG(port, TBT_FLAG_CABLE_ENTRY_DONE);
}

bool tbt_is_active(int port)
{
	return tbt_state[port] != TBT_INACTIVE && tbt_state[port] != TBT_START;
}

bool tbt_entry_is_done(int port)
{
	return tbt_state[port] == TBT_ACTIVE || tbt_state[port] == TBT_INACTIVE;
}

bool tbt_cable_entry_is_done(int port)
{
	return TBT_CHK_FLAG(port, TBT_FLAG_CABLE_ENTRY_DONE);
}

static void tbt_exit_done(int port)
{
	/*
	 * If the EC exits an alt mode autonomously, don't try to enter it
	 * again. If the AP commands the EC to exit DP mode, it might command
	 * the EC to enter again later, so leave the state machine ready for
	 * that possibility.
	 */
	tbt_state[port] = IS_ENABLED(CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY) ?
				  TBT_START :
				  TBT_INACTIVE;
	TBT_CLR_FLAG(port, TBT_FLAG_RETRY_DONE);
	TBT_CLR_FLAG(port, TBT_FLAG_CABLE_ENTRY_DONE);

	if (!TBT_CHK_FLAG(port, TBT_FLAG_EXIT_DONE)) {
		TBT_SET_FLAG(port, TBT_FLAG_EXIT_DONE);
		tbt_prints("Exited alternate mode", port);
		return;
	}

	tbt_prints("alt mode protocol failed!", port);
}

static bool tbt_is_lrd_active_cable(int port)
{
	union tbt_mode_resp_cable cable_mode_resp;

	cable_mode_resp.raw_value =
		pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME);
	if (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE &&
	    cable_mode_resp.tbt_active_passive == TBT_CABLE_ACTIVE)
		return true;

	return false;
}

/* Check if this port requires SOP' mode entry and exit */
static bool tbt_sop_prime_needed(int port)
{
	/*
	 * We require SOP' entry if cable is
	 * active cable, or
	 * an LRD cable (passive in DiscoverIdentity, active in TBT mode)
	 */
	if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE ||
	    tbt_is_lrd_active_cable(port))
		return true;
	return false;
}

/* Check if this port requires SOP'' mode entry and exit */
static bool tbt_sop_prime_prime_needed(int port)
{
	const struct pd_discovery *disc;

	disc = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
	if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE &&
	    disc->identity.product_t1.a_rev20.sop_p_p)
		return true;
	return false;
}

void tbt_exit_mode_request(int port)
{
	TBT_SET_FLAG(port, TBT_FLAG_RETRY_DONE);
	TBT_CLR_FLAG(port, TBT_FLAG_EXIT_DONE);
	/*
	 * If the port has entered USB4 mode with Thunderbolt mode for the
	 * cable, on request to exit, only exit Thunderbolt mode for the
	 * cable.
	 * TODO (b/156749387): Remove once data reset feature is in place.
	 */
	if (tbt_state[port] == TBT_ENTER_SOP) {
		/*
		 * For Linear re-driver cables, the port enters USB4 mode
		 * with Thunderbolt mode for SOP prime. Hence, on request to
		 * exit, only exit Thunderbolt mode SOP prime
		 */
		tbt_state[port] = tbt_sop_prime_prime_needed(port) ?
					  TBT_EXIT_SOP_PRIME_PRIME :
					  TBT_EXIT_SOP_PRIME;
	}
}

static bool tbt_response_valid(int port, enum tcpci_msg_type type, char *cmdt,
			       int vdm_cmd)
{
	enum tbt_states st = tbt_state[port];
	union tbt_mode_resp_cable cable_mode_resp = {
		.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME)
	};

	/*
	 * Check for an unexpected response.
	 * 1. invalid command
	 * 2. invalid Tx type for passive cable
	 * If Thunderbolt is inactive, ignore the command.
	 */
	if ((st != TBT_INACTIVE && state_vdm_cmd[st] != vdm_cmd) ||
	    (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE &&
	     cable_mode_resp.tbt_active_passive == TBT_CABLE_PASSIVE &&
	     type != TCPCI_MSG_SOP)) {
		tbt_exit_done(port);
		return false;
	}
	return true;
}

/* Exit Mode process is complete, but retry Enter Mode process */
static void tbt_retry_enter_mode(int port)
{
	tbt_state[port] = TBT_START;
	TBT_SET_FLAG(port, TBT_FLAG_RETRY_DONE);
}

bool tbt_cable_entry_required_for_usb4(int port)
{
	const struct pd_discovery *disc_sop_prime;

	if (tbt_cable_entry_is_done(port))
		return false;

	/*
	 * For some cables, the TCPM may need to enter TBT mode with the
	 * cable to support USB4 mode with the partner. Request to enter
	 * Thunderbolt mode for the cable prior to entering USB4 for
	 * the port partner if
	 * 1. The cable advertises itself as passive in its Identity VDO
	 * but active in its TBT mode VDO, or
	 * 2. The cable advertises itself as active, but its PD support
	 * is not new enough to support Enter_USB.
	 */
	if (tbt_is_lrd_active_cable(port))
		return true;

	if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE) {
		disc_sop_prime = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
		if (pd_get_vdo_ver(port, TCPCI_MSG_SOP_PRIME) < SVDM_VER_2_0 ||
		    disc_sop_prime->identity.product_t1.a_rev30.vdo_ver <
			    VDO_VERSION_1_3)
			return true;
	}
	return false;
}

void intel_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
		     uint32_t *vdm)
{
	const uint8_t vdm_cmd = PD_VDO_CMD(vdm[0]);

	if (!tbt_response_valid(port, type, "ACK", vdm_cmd))
		return;

	switch (tbt_state[port]) {
	case TBT_ENTER_SOP_PRIME:
		tbt_prints("enter mode SOP'", port);
		/* For LRD cables, Enter mode SOP' -> Enter mode SOP */
		if (tbt_sop_prime_prime_needed(port)) {
			tbt_state[port] = TBT_ENTER_SOP_PRIME_PRIME;
		} else {
			TBT_SET_FLAG(port, TBT_FLAG_CABLE_ENTRY_DONE);
			tbt_state[port] = TBT_ENTER_SOP;
		}
		break;
	case TBT_ENTER_SOP_PRIME_PRIME:
		tbt_prints("enter mode SOP''", port);
		TBT_SET_FLAG(port, TBT_FLAG_CABLE_ENTRY_DONE);
		tbt_state[port] = TBT_ENTER_SOP;
		break;
	case TBT_ENTER_SOP:
		set_tbt_compat_mode_ready(port);
		tbt_state[port] = TBT_ACTIVE;
		tbt_prints("enter mode SOP", port);
		TBT_SET_FLAG(port, TBT_FLAG_RETRY_DONE);
		/* Indicate to PE layer that alt mode is active */
		pd_set_dfp_enter_mode_flag(port, true);
		break;
	case TBT_EXIT_SOP:
		tbt_prints("exit mode SOP", port);
		pd_set_dfp_enter_mode_flag(port, false);

		if (tbt_sop_prime_prime_needed(port)) {
			tbt_state[port] = TBT_EXIT_SOP_PRIME_PRIME;
		} else if (tbt_sop_prime_needed(port)) {
			tbt_state[port] = TBT_EXIT_SOP_PRIME;
		} else {
			set_usb_mux_with_current_data_role(port);
			if (TBT_CHK_FLAG(port, TBT_FLAG_RETRY_DONE))
				/* retried enter mode, still failed, give up */
				tbt_exit_done(port);
			else
				tbt_retry_enter_mode(port);
		}
		break;
	case TBT_EXIT_SOP_PRIME_PRIME:
		tbt_prints("exit mode SOP''", port);
		tbt_state[port] = TBT_EXIT_SOP_PRIME;
		break;
	case TBT_EXIT_SOP_PRIME:
		tbt_prints("exit mode SOP'", port);
		if (TBT_CHK_FLAG(port, TBT_FLAG_RETRY_DONE)) {
			/*
			 * Exit mode process is complete; go to inactive state.
			 */
			tbt_exit_done(port);
			/* Clear Thunderbolt related signals */
			set_usb_mux_with_current_data_role(port);
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
		CPRINTF("%s called with invalid state %d\n", __func__,
			tbt_state[port]);
		tbt_exit_done(port);
		break;
	}
}

void intel_vdm_naked(int port, enum tcpci_msg_type type, uint8_t vdm_cmd)
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
		tbt_state[port] = TBT_PREPARE_EXIT_MODE;
		break;
	case TBT_EXIT_SOP:
		/* Exit SOP got NAK'ed */
		tbt_prints("exit mode SOP failed", port);

		if (tbt_sop_prime_prime_needed(port)) {
			tbt_state[port] = TBT_EXIT_SOP_PRIME_PRIME;
		} else if (tbt_sop_prime_needed(port)) {
			tbt_state[port] = TBT_EXIT_SOP_PRIME;
		} else {
			set_usb_mux_with_current_data_role(port);
			if (TBT_CHK_FLAG(port, TBT_FLAG_RETRY_DONE))
				/* Retried enter mode, still failed, give up */
				tbt_exit_done(port);
			else
				tbt_retry_enter_mode(port);
		}
		break;
	case TBT_EXIT_SOP_PRIME_PRIME:
		tbt_prints("exit mode SOP'' failed", port);
		tbt_state[port] = TBT_EXIT_SOP_PRIME;
		break;
	case TBT_EXIT_SOP_PRIME:
		set_usb_mux_with_current_data_role(port);
		if (TBT_CHK_FLAG(port, TBT_FLAG_RETRY_DONE)) {
			/*
			 * Exit mode process is complete; go to inactive state.
			 */
			tbt_prints("exit mode SOP' failed", port);
			tbt_exit_done(port);
		} else {
			tbt_retry_enter_mode(port);
		}
		break;
	default:
		CPRINTS("C%d: NAK for cmd %d in state %d", port, vdm_cmd,
			tbt_state[port]);
		tbt_exit_done(port);
		break;
	}
}

static bool tbt_mode_is_supported(int port, int vdo_count)
{
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPCI_MSG_SOP);

	if (!disc->identity.idh.modal_support)
		return false;

	if (get_tbt_cable_speed(port) < TBT_SS_U31_GEN1)
		return false;

	/*
	 * TBT4 PD Discovery Flow Application Notes Revision 0.9:
	 * Figure 2: for active cable, SOP' should support
	 * SVID USB_VID_INTEL to enter Thunderbolt alt mode
	 */
	if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE &&
	    !pd_is_mode_discovered_for_svid(port, TCPCI_MSG_SOP_PRIME,
					    USB_VID_INTEL))
		return false;

	return true;
}

enum dpm_msg_setup_status tbt_setup_next_vdm(int port, int *vdo_count,
					     uint32_t *vdm,
					     enum tcpci_msg_type *tx_type)
{
	int vdo_count_ret = 0;

	*tx_type = TCPCI_MSG_SOP;

	if (*vdo_count < VDO_MAX_SIZE)
		return MSG_SETUP_ERROR;

	switch (tbt_state[port]) {
	case TBT_START:
		if (!tbt_mode_is_supported(port, *vdo_count))
			return MSG_SETUP_UNSUPPORTED;

		if (!TBT_CHK_FLAG(port, TBT_FLAG_RETRY_DONE))
			tbt_prints("attempt to enter mode", port);
		else
			tbt_prints("retry to enter mode", port);

		/*
		 * Enter safe mode before sending Enter mode SOP/SOP'/SOP''
		 * Ref: Tiger Lake Platform PD Controller Interface
		 * Requirements for Integrated USB C, section A.1.2 TBT as DFP.
		 */
		usb_mux_set_safe_mode(port);

		/* Active cable and LRD cables send Enter Mode SOP' first */
		if (tbt_sop_prime_needed(port)) {
			tbt_state[port] = TBT_ENTER_SOP_PRIME;
		} else {
			/* Passive cable send Enter Mode SOP */
			tbt_state[port] = TBT_ENTER_SOP;
		}

		return MSG_SETUP_MUX_WAIT;
	case TBT_ENTER_SOP_PRIME:
		vdo_count_ret =
			enter_tbt_compat_mode(port, TCPCI_MSG_SOP_PRIME, vdm);
		*tx_type = TCPCI_MSG_SOP_PRIME;
		break;
	case TBT_ENTER_SOP_PRIME_PRIME:
		vdo_count_ret = enter_tbt_compat_mode(
			port, TCPCI_MSG_SOP_PRIME_PRIME, vdm);
		*tx_type = TCPCI_MSG_SOP_PRIME_PRIME;
		break;
	case TBT_ENTER_SOP:
		vdo_count_ret = enter_tbt_compat_mode(port, TCPCI_MSG_SOP, vdm);
		break;
	case TBT_ACTIVE:
		/*
		 * Since we had successfully entered mode, consider ourselves
		 * done with any retires.
		 */
		TBT_SET_FLAG(port, TBT_FLAG_RETRY_DONE);
		__fallthrough;
	case TBT_PREPARE_EXIT_MODE:
		/*
		 * Called to exit Thunderbolt alt mode, either when the mode is
		 * active and the system is shutting down, or when an initial
		 * request to enter the mode is NAK'ed. This can happen if EC
		 * is restarted while Thunderbolt mode is active.
		 */
		usb_mux_set_safe_mode_exit(port);

		tbt_state[port] = TBT_EXIT_SOP;
		return MSG_SETUP_MUX_WAIT;
	case TBT_EXIT_SOP:
		/* DPM will only call this after safe state set is done */
		vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE) |
			 VDO_OPOS(tbt_opos) | VDO_CMDT(CMDT_INIT) |
			 VDO_SVDM_VERS_MAJOR(
				 pd_get_vdo_ver(port, TCPCI_MSG_SOP));
		vdo_count_ret = 1;
		break;
	case TBT_EXIT_SOP_PRIME_PRIME:
		vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE) |
			 VDO_OPOS(tbt_opos) | VDO_CMDT(CMDT_INIT) |
			 VDO_SVDM_VERS_MAJOR(pd_get_vdo_ver(
				 port, TCPCI_MSG_SOP_PRIME_PRIME));
		vdo_count_ret = 1;
		*tx_type = TCPCI_MSG_SOP_PRIME_PRIME;
		break;
	case TBT_EXIT_SOP_PRIME:
		vdm[0] = VDO(USB_VID_INTEL, 1, CMD_EXIT_MODE) |
			 VDO_OPOS(tbt_opos) | VDO_CMDT(CMDT_INIT) |
			 VDO_SVDM_VERS_MAJOR(
				 pd_get_vdo_ver(port, TCPCI_MSG_SOP_PRIME));
		vdo_count_ret = 1;
		*tx_type = TCPCI_MSG_SOP_PRIME;
		break;
	case TBT_INACTIVE:
		/* Thunderbolt mode is inactive */
		return MSG_SETUP_UNSUPPORTED;
	default:
		CPRINTF("%s called with invalid state %d\n", __func__,
			tbt_state[port]);
		return MSG_SETUP_ERROR;
	}

	if (vdo_count_ret) {
		*vdo_count = vdo_count_ret;
		return MSG_SETUP_SUCCESS;
	}

	return MSG_SETUP_UNSUPPORTED;
}

uint32_t pd_get_tbt_mode_vdo(int port, enum tcpci_msg_type type)
{
	uint32_t tbt_mode_vdo[VDO_MAX_OBJECTS];

	return pd_get_mode_vdo_for_svid(port, type, USB_VID_INTEL,
					tbt_mode_vdo) ?
		       tbt_mode_vdo[0] :
		       0;
}

void set_tbt_compat_mode_ready(int port)
{
	if (IS_ENABLED(CONFIG_USBC_SS_MUX) &&
	    IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)) {
		/* Connect the SBU and USB lines to the connector. */
		typec_set_sbu(port, true);

		/* Set usb mux to Thunderbolt-compatible mode */
		usb_mux_set(port, USB_PD_MUX_TBT_COMPAT_ENABLED,
			    USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	}
}

/*
 * Ref: USB Type-C Cable and Connector Specification
 * Figure F-1 TBT3 Discovery Flow
 */
static bool is_tbt_cable_superspeed(int port)
{
	const struct pd_discovery *disc;

	if (!IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) ||
	    !IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		return false;

	disc = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);

	/* Product type is Active cable, hence don't check for speed */
	if (disc->identity.idh.product_type == IDH_PTYPE_ACABLE)
		return true;

	if (disc->identity.idh.product_type != IDH_PTYPE_PCABLE)
		return false;

	if (IS_ENABLED(CONFIG_USB_PD_REV30) &&
	    pd_get_rev(port, TCPCI_MSG_SOP_PRIME) == PD_REV30)
		return disc->identity.product_t1.p_rev30.ss ==
			       USB_R30_SS_U32_U40_GEN1 ||
		       disc->identity.product_t1.p_rev30.ss ==
			       USB_R30_SS_U32_U40_GEN2 ||
		       disc->identity.product_t1.p_rev30.ss ==
			       USB_R30_SS_U40_GEN3;

	return disc->identity.product_t1.p_rev20.ss == USB_R20_SS_U31_GEN1 ||
	       disc->identity.product_t1.p_rev20.ss == USB_R20_SS_U31_GEN1_GEN2;
}

static enum tbt_compat_cable_speed usb_rev30_to_tbt_speed(enum usb_rev30_ss ss)
{
	switch (ss) {
	case USB_R30_SS_U32_U40_GEN1:
		return TBT_SS_U31_GEN1;
	case USB_R30_SS_U32_U40_GEN2:
		return TBT_SS_U32_GEN1_GEN2;
	case USB_R30_SS_U40_GEN3:
		return TBT_SS_TBT_GEN3;
	default:
		return TBT_SS_U32_GEN1_GEN2;
	}
}

enum tbt_compat_cable_speed get_tbt_cable_speed(int port)
{
	union tbt_mode_resp_cable cable_mode_resp;
	enum tbt_compat_cable_speed max_tbt_speed;
	enum tbt_compat_cable_speed cable_tbt_speed;

	if (!is_tbt_cable_superspeed(port))
		return TBT_SS_RES_0;

	cable_mode_resp.raw_value =
		pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME);
	max_tbt_speed = board_get_max_tbt_speed(port);

	/*
	 * Ref: TBT4 PD Discovery Flow Application Notes Revision 0.9, Figure 2
	 * For passive cable, if cable doesn't support USB_VID_INTEL, enter
	 * Thunderbolt alternate mode with speed from USB Highest Speed field of
	 * the Passive Cable VDO
	 * For active cable, if the cable doesn't support USB_VID_INTEL, do not
	 * enter Thunderbolt alternate mode.
	 */
	if (!cable_mode_resp.raw_value) {
		const struct pd_discovery *disc;

		if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE)
			return TBT_SS_RES_0;

		disc = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
		cable_tbt_speed = usb_rev30_to_tbt_speed(
			disc->identity.product_t1.p_rev30.ss);
	} else {
		cable_tbt_speed = cable_mode_resp.tbt_cable_speed;
	}

	return max_tbt_speed < cable_tbt_speed ? max_tbt_speed :
						 cable_tbt_speed;
}

/* Note: Assumes that pins have already been set in safe state */
int enter_tbt_compat_mode(int port, enum tcpci_msg_type sop, uint32_t *payload)
{
	union tbt_dev_mode_enter_cmd enter_dev_mode = { .raw_value = 0 };
	union tbt_mode_resp_device dev_mode_resp;
	union tbt_mode_resp_cable cable_mode_resp;
	enum tcpci_msg_type enter_mode_sop =
		sop == TCPCI_MSG_SOP_PRIME_PRIME ? TCPCI_MSG_SOP_PRIME : sop;

	/* Table F-12 TBT3 Cable Enter Mode Command */
	/*
	 * The port doesn't query Discover SOP'' to the cable so, the port
	 * doesn't have opos for SOP''. Hence, send Enter Mode SOP'' with same
	 * opos and revision as SOP'.
	 */
	payload[0] =
		VDO(USB_VID_INTEL, 1, CMD_ENTER_MODE | VDO_OPOS(tbt_opos)) |
		VDO_CMDT(CMDT_INIT) |
		VDO_SVDM_VERS_MAJOR(pd_get_vdo_ver(port, enter_mode_sop));

	/* For TBT3 Cable Enter Mode Command, number of Objects is 1 */
	if ((sop == TCPCI_MSG_SOP_PRIME) || (sop == TCPCI_MSG_SOP_PRIME_PRIME))
		return 1;

	dev_mode_resp.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP);
	cable_mode_resp.raw_value =
		pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME);

	/* Table F-13 TBT3 Device Enter Mode Command */
	enter_dev_mode.vendor_spec_b1 = dev_mode_resp.vendor_spec_b1;
	enter_dev_mode.vendor_spec_b0 = dev_mode_resp.vendor_spec_b0;
	enter_dev_mode.intel_spec_b0 = dev_mode_resp.intel_spec_b0;

	if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE ||
	    cable_mode_resp.tbt_active_passive == TBT_CABLE_ACTIVE)
		enter_dev_mode.cable = TBT_ENTER_ACTIVE_CABLE;

	enter_dev_mode.lsrx_comm = cable_mode_resp.lsrx_comm;
	enter_dev_mode.retimer_type = cable_mode_resp.retimer_type;
	enter_dev_mode.tbt_cable = cable_mode_resp.tbt_cable;
	enter_dev_mode.tbt_rounded = cable_mode_resp.tbt_rounded;
	enter_dev_mode.tbt_cable_speed = get_tbt_cable_speed(port);
	enter_dev_mode.tbt_alt_mode = TBT_ALTERNATE_MODE;

	payload[1] = enter_dev_mode.raw_value;

	/* For TBT3 Device Enter Mode Command, number of Objects are 2 */
	return 2;
}

enum tbt_compat_rounded_support get_tbt_rounded_support(int port)
{
	union tbt_mode_resp_cable cable_mode_resp = {
		.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME)
	};

	/* tbt_rounded_support is zero when uninitialized */
	return cable_mode_resp.tbt_rounded;
}

__overridable enum tbt_compat_cable_speed board_get_max_tbt_speed(int port)
{
	return TBT_SS_TBT_GEN3;
}
