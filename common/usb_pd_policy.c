/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "registers.h"
#include "rsa.h"
#include "sha256.h"
#include "system.h"
#include "task.h"
#include "tcpm.h"
#include "timer.h"
#include "util.h"
#include "usb_api.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "version.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

/*
 * This file is currently only used for TCPMv1, and would need changes before
 * being used for TCPMv2. One example: PD_FLAGS_* are TCPMv1 only.
 */
#ifndef CONFIG_USB_PD_TCPMV1
#error This file must only be used with TCPMv1
#endif

static int rw_flash_changed = 1;

__overridable void pd_check_pr_role(int port,
	enum pd_power_role pr_role, int flags)
{
	/*
	 * If partner is dual-role power and dualrole toggling is on, consider
	 * if a power swap is necessary.
	 */
	if ((flags & PD_FLAGS_PARTNER_DR_POWER) &&
	    pd_get_dual_role(port) == PD_DRP_TOGGLE_ON) {
		/*
		 * If we are a sink and partner is not unconstrained, then
		 * swap to become a source. If we are source and partner is
		 * unconstrained, swap to become a sink.
		 */
		int partner_unconstrained = flags & PD_FLAGS_PARTNER_UNCONSTR;

		if ((!partner_unconstrained && pr_role == PD_ROLE_SINK) ||
		     (partner_unconstrained && pr_role == PD_ROLE_SOURCE))
			pd_request_power_swap(port);
	}
}

__overridable void pd_check_dr_role(int port,
	enum pd_data_role dr_role, int flags)
{
	/* If UFP, try to switch to DFP */
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_UFP)
		pd_request_data_swap(port);
}

#ifdef CONFIG_MKBP_EVENT
static int dp_alt_mode_entry_get_next_event(uint8_t *data)
{
	return EC_SUCCESS;
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_DP_ALT_MODE_ENTERED,
		     dp_alt_mode_entry_get_next_event);
#endif /* CONFIG_MKBP_EVENT */

#ifdef CONFIG_USB_PD_DUAL_ROLE
/* Last received source cap */
static uint32_t pd_src_caps[CONFIG_USB_PD_PORT_MAX_COUNT][PDO_MAX_OBJECTS];
static uint8_t pd_src_cap_cnt[CONFIG_USB_PD_PORT_MAX_COUNT];

const uint32_t * const pd_get_src_caps(int port)
{
	return pd_src_caps[port];
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
	int i;

	pd_src_cap_cnt[port] = cnt;

	for (i = 0; i < cnt; i++)
		pd_src_caps[port][i] = *src_caps++;
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return pd_src_cap_cnt[port];
}
#endif /* CONFIG_USB_PD_DUAL_ROLE */

static struct pd_cable cable[CONFIG_USB_PD_PORT_MAX_COUNT];

enum pd_rev_type get_usb_pd_cable_revision(int port)
{
	return cable[port].rev;
}

bool consume_sop_prime_repeat_msg(int port, uint8_t msg_id)
{

	if (cable[port].last_sop_p_msg_id != msg_id) {
		cable[port].last_sop_p_msg_id = msg_id;
		return false;
	}
	CPRINTF("C%d SOP Prime repeat msg_id %d\n", port, msg_id);
	return true;
}

bool consume_sop_prime_prime_repeat_msg(int port, uint8_t msg_id)
{

	if (cable[port].last_sop_p_p_msg_id != msg_id) {
		cable[port].last_sop_p_p_msg_id = msg_id;
		return false;
	}
	CPRINTF("C%d SOP Prime Prime repeat msg_id %d\n", port, msg_id);
	return true;
}

__maybe_unused static uint8_t is_sop_prime_ready(int port)
{
	/*
	 * Ref: USB PD 3.0 sec 2.5.4: When an Explicit Contract is in place the
	 * VCONN Source (either the DFP or the UFP) can communicate with the
	 * Cable Plug(s) using SOP’/SOP’’ Packets
	 *
	 * Ref: USB PD 2.0 sec 2.4.4: When an Explicit Contract is in place the
	 * DFP (either the Source or the Sink) can communicate with the
	 * Cable Plug(s) using SOP’/SOP” Packets.
	 * Sec 3.6.11 : Before communicating with a Cable Plug a Port Should
	 * ensure that it is the Vconn Source
	 */
	return (pd_get_vconn_state(port) && (IS_ENABLED(CONFIG_USB_PD_REV30)
		|| (pd_get_data_role(port) == PD_ROLE_DFP)));
}

void reset_pd_cable(int port)
{
	memset(&cable[port], 0, sizeof(cable[port]));
	cable[port].last_sop_p_msg_id = INVALID_MSG_ID_COUNTER;
	cable[port].last_sop_p_p_msg_id = INVALID_MSG_ID_COUNTER;
}

bool should_enter_usb4_mode(int port)
{
	return IS_ENABLED(CONFIG_USB_PD_USB4) &&
		cable[port].flags & CABLE_FLAGS_ENTER_USB_MODE;
}

void enable_enter_usb4_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_USB4))
		cable[port].flags |= CABLE_FLAGS_ENTER_USB_MODE;
}

void disable_enter_usb4_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_USB4))
		cable[port].flags &= ~CABLE_FLAGS_ENTER_USB_MODE;
}

#ifdef CONFIG_USB_PD_ALT_MODE

#ifdef CONFIG_USB_PD_ALT_MODE_DFP

static struct pd_discovery
	discovery[CONFIG_USB_PD_PORT_MAX_COUNT][DISCOVERY_TYPE_COUNT];
static struct partner_active_modes
	partner_amodes[CONFIG_USB_PD_PORT_MAX_COUNT][AMODE_TYPE_COUNT];

static bool is_vdo_present(int cnt, int index)
{
	return cnt > index;
}

static bool is_modal(int port, int cnt, const uint32_t *payload)
{
	return is_vdo_present(cnt, VDO_INDEX_IDH) &&
		PD_IDH_IS_MODAL(payload[VDO_INDEX_IDH]);
}

static bool is_tbt_compat_mode(int port, int cnt, const uint32_t *payload)
{
	/*
	 * Ref: USB Type-C cable and connector specification
	 * F.2.5 TBT3 Device Discover Mode Responses
	 */
	return is_vdo_present(cnt, VDO_INDEX_IDH) &&
		PD_VDO_RESP_MODE_INTEL_TBT(payload[VDO_INDEX_IDH]);
}

static bool cable_supports_tbt_speed(int port)
{
	enum tbt_compat_cable_speed tbt_cable_speed =
				get_tbt_cable_speed(port);

	return (tbt_cable_speed == TBT_SS_TBT_GEN3 ||
		tbt_cable_speed == TBT_SS_U32_GEN1_GEN2);
}

static bool is_tbt_compat_enabled(int port)
{
	return (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) &&
	       (cable[port].flags & CABLE_FLAGS_TBT_COMPAT_ENABLE));
}

static void enable_tbt_compat_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE))
		cable[port].flags |= CABLE_FLAGS_TBT_COMPAT_ENABLE;
}

static inline void disable_tbt_compat_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE))
		cable[port].flags &= ~CABLE_FLAGS_TBT_COMPAT_ENABLE;
}

static inline void limit_tbt_cable_speed(int port)
{
	/* Cable flags are cleared when cable reset is called */
	cable[port].flags |= CABLE_FLAGS_TBT_COMPAT_LIMIT_SPEED;
}

static inline bool is_limit_tbt_cable_speed(int port)
{
	return !!(cable[port].flags & CABLE_FLAGS_TBT_COMPAT_LIMIT_SPEED);
}

static bool is_intel_svid(int port, enum tcpm_transmit_type type)
{
	int i;

	for (i = 0; i < discovery[port][type].svid_cnt; i++) {
		if (pd_get_svid(port, i, type) == USB_VID_INTEL)
			return true;
	}

	return false;
}

static inline bool is_usb4_mode_enabled(int port)
{
	return (IS_ENABLED(CONFIG_USB_PD_USB4) &&
	       (cable[port].flags & CABLE_FLAGS_USB4_CAPABLE));
}

static inline void enable_usb4_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_USB4))
		cable[port].flags |= CABLE_FLAGS_USB4_CAPABLE;
}

static inline void disable_usb4_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_USB4))
		cable[port].flags &= ~CABLE_FLAGS_USB4_CAPABLE;
}

/*
 * Ref: USB Type-C Cable and Connector Specification
 * Figure 5-1 USB4 Discovery and Entry Flow Model.
 *
 * Note: USB Type-C Cable and Connector Specification
 * doesn't include details for Revision 2 cables.
 *
 *                         Passive Cable
 *                                |
 *                -----------------------------------
 *                |                                 |
 *           Revision 2                        Revision 3
 *          USB Signalling                   USB Signalling
 *             |                                     |
 *     ------------------            -------------------------
 *     |       |        |            |       |       |       |
 * USB2.0   USB3.1    USB3.1       USB3.2   USB4   USB3.2   USB2
 *   |      Gen1      Gen1 Gen2    Gen2     Gen3   Gen1       |
 *   |       |          |           |        |       |       Exit
 *   --------           ------------         --------        USB4
 *      |                    |                  |          Discovery.
 *    Exit          Is DFP Gen3 Capable?     Enter USB4
 *    USB4                  |                with respective
 *   Discovery.   --- No ---|--- Yes ---     cable speed.
 *                |                    |
 *    Enter USB4 with             Is Cable TBT3
 *    respective cable                 |
 *    speed.                 --- No ---|--- Yes ---
 *                           |                    |
 *                   Enter USB4 with        Enter USB4 with
 *                   TBT Gen2 passive       TBT Gen3 passive
 *                   cable.                 cable.
 *
 */
static bool is_cable_ready_to_enter_usb4(int port, int cnt)
{
	/* TODO: USB4 enter mode for Active cables */
	struct pd_discovery *disc = &discovery[port][TCPC_TX_SOP_PRIME];
	if (IS_ENABLED(CONFIG_USB_PD_USB4) &&
	   (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE) &&
	    is_vdo_present(cnt, VDO_INDEX_PTYPE_CABLE1)) {
		switch (cable[port].rev) {
		case PD_REV30:
			switch (disc->identity.product_t1.p_rev30.ss) {
			case USB_R30_SS_U40_GEN3:
			case USB_R30_SS_U32_U40_GEN1:
				return true;
			case USB_R30_SS_U32_U40_GEN2:
				/* Check if DFP is Gen 3 capable */
				if (IS_ENABLED(CONFIG_USB_PD_TBT_GEN3_CAPABLE))
					return false;
				return true;
			default:
				disable_usb4_mode(port);
				return false;
			}
		case PD_REV20:
			switch (disc->identity.product_t1.p_rev20.ss) {
			case USB_R20_SS_U31_GEN1_GEN2:
				/* Check if DFP is Gen 3 capable */
				if (IS_ENABLED(CONFIG_USB_PD_TBT_GEN3_CAPABLE))
					return false;
				return true;
			default:
				disable_usb4_mode(port);
				return false;
		}
		default:
			disable_usb4_mode(port);
		}
	}
	return false;
}

void pd_dfp_discovery_init(int port)
{
	memset(&discovery[port], 0, sizeof(struct pd_discovery));
	memset(&partner_amodes[port], 0, sizeof(partner_amodes[0]));
}

static int dfp_discover_ident(uint32_t *payload)
{
	payload[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT);
	return 1;
}

static int dfp_discover_svids(uint32_t *payload)
{
	payload[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_SVID);
	return 1;
}

struct pd_discovery *pd_get_am_discovery(int port, enum tcpm_transmit_type type)
{
	return &discovery[port][type];
}

struct partner_active_modes *pd_get_partner_active_modes(int port,
		enum tcpm_transmit_type type)
{
	assert(type < AMODE_TYPE_COUNT);
	return &partner_amodes[port][type];
}

/* Note: Enter mode flag is not needed by TCPMv1 */
void pd_set_dfp_enter_mode_flag(int port, bool set)
{
}

/**
 * Return the discover alternate mode payload data
 *
 * @param port    USB-C port number
 * @param payload Pointer to payload data to fill
 * @return 1 if valid SVID present else 0
 */
static int dfp_discover_modes(int port, uint32_t *payload)
{
	struct pd_discovery *disc = pd_get_am_discovery(port, TCPC_TX_SOP);
	uint16_t svid = disc->svids[disc->svid_idx].svid;

	if (disc->svid_idx >= disc->svid_cnt)
		return 0;

	payload[0] = VDO(svid, 1, CMD_DISCOVER_MODES);

	return 1;
}

static bool is_usb4_vdo(int port, int cnt, uint32_t *payload)
{
	enum idh_ptype ptype = PD_IDH_PTYPE(payload[VDO_I(IDH)]);

	if (IS_PD_IDH_UFP_PTYPE(ptype)) {
		/*
		 * Ref: USB Type-C Cable and Connector Specification
		 * Figure 5-1 USB4 Discovery and Entry Flow Model
		 * Device USB4 VDO detection.
		 */
		return IS_ENABLED(CONFIG_USB_PD_USB4) &&
			is_vdo_present(cnt, VDO_INDEX_PTYPE_UFP1_VDO) &&
			PD_PRODUCT_IS_USB4(payload[VDO_INDEX_PTYPE_UFP1_VDO]);
	}
	return false;
}

static int process_am_discover_ident_sop(int port, int cnt,
					uint32_t head, uint32_t *payload,
					enum tcpm_transmit_type *rtype)
{
	pd_dfp_discovery_init(port);
	dfp_consume_identity(port, TCPC_TX_SOP, cnt, payload);

	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP) && is_sop_prime_ready(port) &&
	    board_is_tbt_usb4_port(port)) {

		/* Enable USB4 mode if USB4 VDO present and port partner
		 * supports USB Rev 3.0.
		 */
		if (is_usb4_vdo(port, cnt, payload) &&
		    PD_HEADER_REV(head) == PD_REV30)
			enable_usb4_mode(port);

		/*
		 * Enable Thunderbolt-compatible mode if the modal operation is
		 * supported.
		 */
		if (is_modal(port, cnt, payload))
			enable_tbt_compat_mode(port);

		if (is_modal(port, cnt, payload) ||
		    is_usb4_vdo(port, cnt, payload)) {
			*rtype = TCPC_TX_SOP_PRIME;
			return dfp_discover_ident(payload);
		}
	}

	return dfp_discover_svids(payload);
}

static int process_am_discover_ident_sop_prime(int port, int cnt,
					uint32_t head, uint32_t *payload)
{
	dfp_consume_identity(port, TCPC_TX_SOP_PRIME, cnt, payload);
	cable[port].rev = PD_HEADER_REV(head);

	/*
	 * Enter USB4 mode if the cable supports USB4 operation and has USB4
	 * VDO.
	 */
	if (is_usb4_mode_enabled(port) &&
	    is_cable_ready_to_enter_usb4(port, cnt)) {
		enable_enter_usb4_mode(port);
		usb_mux_set_safe_mode(port);
		/*
		 * To change the mode of operation from USB4 the port needs to
		 * be reconfigured.
		 * Ref: USB Type-C Cable and Connectot Spec section 5.4.4.
		 */
		disable_tbt_compat_mode(port);
		return 0;
	}

	/*
	 * Disable Thunderbolt-compatible mode if the cable does not support
	 * superspeed.
	 */
	if (is_tbt_compat_enabled(port) && !is_tbt_cable_superspeed(port))
		disable_tbt_compat_mode(port);

	return dfp_discover_svids(payload);
}

static int process_am_discover_svids(int port, int cnt, uint32_t *payload,
				enum tcpm_transmit_type sop,
				enum tcpm_transmit_type *rtype)
{
	/*
	 * The pd_discovery structure stores SOP and SOP' discovery results
	 * separately, but TCPMv1 depends on one-dimensional storage of SVIDs
	 * and modes. Therefore, always use TCPC_TX_SOP in TCPMv1.
	 */
	dfp_consume_svids(port, sop, cnt, payload);

	/*
	 * Ref: USB Type-C Cable and Connector Specification,
	 * figure F-1: TBT3 Discovery Flow
	 *
	 * For USB4 mode if device or cable doesn't have Intel SVID,
	 * disable Thunderbolt-Compatible mode directly enter USB4 mode
	 * with USB3.2 Gen1/Gen2 speed.
	 *
	 * For Thunderbolt-compatible, check if 0x8087 is received for
	 * Discover SVID SOP. If not, disable Thunderbolt-compatible mode
	 *
	 * If 0x8087 is not received for Discover SVID SOP' limit to TBT
	 * passive Gen 2 cable.
	 */
	if (is_tbt_compat_enabled(port)) {
		bool intel_svid = is_intel_svid(port, sop);
		if (!intel_svid) {
			if (is_usb4_mode_enabled(port)) {
				disable_tbt_compat_mode(port);
				cable[port].cable_mode_resp.tbt_cable_speed =
					TBT_SS_U32_GEN1_GEN2;
				enable_enter_usb4_mode(port);
				usb_mux_set_safe_mode(port);
				return 0;
			}

			if (sop == TCPC_TX_SOP_PRIME)
				limit_tbt_cable_speed(port);
			else
				disable_tbt_compat_mode(port);
		} else if (sop == TCPC_TX_SOP) {
			*rtype = TCPC_TX_SOP_PRIME;
			return dfp_discover_svids(payload);
		}
	}

	return dfp_discover_modes(port, payload);
}

static int process_tbt_compat_discover_modes(int port,
				enum tcpm_transmit_type sop, uint32_t *payload,
				enum tcpm_transmit_type *rtype)
{
	int rsize;

	/* Initialize transmit type to SOP */
	*rtype = TCPC_TX_SOP;

	/*
	 * For active cables, Enter mode: SOP', SOP'', SOP
	 * Ref: USB Type-C Cable and Connector Specification, figure F-1: TBT3
	 * Discovery Flow and Section F.2.7 TBT3 Cable Enter Mode Command.
	 */
	if (sop == TCPC_TX_SOP_PRIME) {
		/* Store Discover Mode SOP' response */
		cable[port].cable_mode_resp.raw_value = payload[1];

		if (is_usb4_mode_enabled(port)) {
			/*
			 * If Cable is not Thunderbolt Gen 3
			 * capable or Thunderbolt Gen1_Gen2
			 * capable, disable USB4 mode and
			 * continue flow for
			 * Thunderbolt-compatible mode
			 */
			if (cable_supports_tbt_speed(port)) {
				enable_enter_usb4_mode(port);
				usb_mux_set_safe_mode(port);
				return 0;
			}
			disable_usb4_mode(port);
		}

		/*
		 * Send TBT3 Cable Enter Mode (SOP') for active cables,
		 * otherwise send TBT3 Device Enter Mode (SOP).
		 */
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE)
			*rtype = TCPC_TX_SOP_PRIME;

		rsize = enter_tbt_compat_mode(port, *rtype, payload);
	} else {
		/* Store Discover Mode SOP response */
		cable[port].dev_mode_resp.raw_value = payload[1];

		if (is_limit_tbt_cable_speed(port)) {
			/*
			 * Passive cable has Nacked for Discover SVID.
			 * No need to do Discover modes of cable.
			 * Enter into device Thunderbolt-compatible mode.
			 */
			rsize = enter_tbt_compat_mode(port, *rtype, payload);
		} else {
			/* Discover modes for SOP' */
			discovery[port][TCPC_TX_SOP].svid_idx--;
			rsize = dfp_discover_modes(port, payload);
			*rtype = TCPC_TX_SOP_PRIME;
		}
	}

	return rsize;
}

static int obj_cnt_enter_tbt_compat_mode(int port,
		enum tcpm_transmit_type sop, uint32_t *payload,
		enum tcpm_transmit_type *rtype)
{
	struct pd_discovery *disc = &discovery[port][TCPC_TX_SOP_PRIME];

	/* Enter mode SOP' for active cables */
	if (sop == TCPC_TX_SOP_PRIME) {
		/* Check if the cable has a SOP'' controller */
		if (disc->identity.product_t1.a_rev20.sop_p_p)
			*rtype = TCPC_TX_SOP_PRIME_PRIME;
		return enter_tbt_compat_mode(port, *rtype, payload);
	}

	/* Enter Mode SOP'' for active cables with SOP'' controller */
	if (sop == TCPC_TX_SOP_PRIME_PRIME)
		return enter_tbt_compat_mode(port, *rtype, payload);

	/* Update Mux state to Thunderbolt-compatible mode. */
	set_tbt_compat_mode_ready(port);
	/* No response once device (and cable) acks */
	return 0;
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload,
		uint32_t head, enum tcpm_transmit_type *rtype)
{
	int cmd = PD_VDO_CMD(payload[0]);
	int cmd_type = PD_VDO_CMDT(payload[0]);
	int (*func)(int port, uint32_t *payload) = NULL;

	int rsize = 1; /* VDM header at a minimum */

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	enum tcpm_transmit_type sop = PD_HEADER_GET_SOP(head);
#endif

	/* Transmit SOP messages by default */
	*rtype = TCPC_TX_SOP;

	payload[0] &= ~VDO_CMDT_MASK;
	*rpayload = payload;

	if (cmd_type == CMDT_INIT) {
		switch (cmd) {
		case CMD_DISCOVER_IDENT:
			func = svdm_rsp.identity;
			break;
		case CMD_DISCOVER_SVID:
			func = svdm_rsp.svids;
			break;
		case CMD_DISCOVER_MODES:
			func = svdm_rsp.modes;
			break;
		case CMD_ENTER_MODE:
			func = svdm_rsp.enter_mode;
			break;
		case CMD_DP_STATUS:
			if (svdm_rsp.amode)
				func = svdm_rsp.amode->status;
			break;
		case CMD_DP_CONFIG:
			if (svdm_rsp.amode)
				func = svdm_rsp.amode->config;
			break;
		case CMD_EXIT_MODE:
			func = svdm_rsp.exit_mode;
			break;
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		case CMD_ATTENTION:
			/*
			 * attention is only SVDM with no response
			 * (just goodCRC) return zero here.
			 */
			dfp_consume_attention(port, payload);
			return 0;
#endif
		default:
			CPRINTF("ERR:CMD:%d\n", cmd);
			rsize = 0;
		}
		if (func)
			rsize = func(port, payload);
		else /* not supported : NACK it */
			rsize = 0;
		if (rsize >= 1)
			payload[0] |= VDO_CMDT(CMDT_RSP_ACK);
		else if (!rsize) {
			payload[0] |= VDO_CMDT(CMDT_RSP_NAK);
			rsize = 1;
		} else {
			payload[0] |= VDO_CMDT(CMDT_RSP_BUSY);
			rsize = 1;
		}
		payload[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
	} else if (cmd_type == CMDT_RSP_ACK) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		struct svdm_amode_data *modep;

		modep = pd_get_amode_data(port, TCPC_TX_SOP,
				PD_VDO_VID(payload[0]));
#endif
		switch (cmd) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		case CMD_DISCOVER_IDENT:
			/* Received a SOP' Discover Ident msg */
			if (sop == TCPC_TX_SOP_PRIME) {
				rsize = process_am_discover_ident_sop_prime(
						port, cnt, head, payload);
			/* Received a SOP Discover Ident Message */
			} else {
				rsize = process_am_discover_ident_sop(port,
						cnt, head, payload, rtype);
			}
#ifdef CONFIG_CHARGE_MANAGER
			if (pd_charge_from_device(pd_get_identity_vid(port),
						  pd_get_identity_pid(port)))
				charge_manager_update_dualrole(port,
							       CAP_DEDICATED);
#endif
			break;
		case CMD_DISCOVER_SVID:
			rsize = process_am_discover_svids(port, cnt, payload,
							  sop, rtype);
			break;
		case CMD_DISCOVER_MODES:
			dfp_consume_modes(port, sop, cnt, payload);
			if (is_tbt_compat_enabled(port) &&
				is_tbt_compat_mode(port, cnt, payload)) {
				rsize = process_tbt_compat_discover_modes(
					      port, sop, payload, rtype);
				break;
			}

			rsize = dfp_discover_modes(port, payload);
			/* enter the default mode for DFP */
			if (!rsize) {
				/*
				 * Disabling Thunderbolt-Compatible mode if
				 * discover mode response doesn't include Intel
				 * SVID.
				 */
				disable_tbt_compat_mode(port);
				payload[0] = pd_dfp_enter_mode(port,
						TCPC_TX_SOP, 0, 0);
				if (payload[0])
					rsize = 1;
			}
			break;
		case CMD_ENTER_MODE:
			if (is_tbt_compat_enabled(port)) {
				rsize = obj_cnt_enter_tbt_compat_mode(port,
							sop, payload, rtype);
			/*
			 * Continue with PD flow if Thunderbolt-compatible mode
			 * is disabled.
			 */
			} else if (!modep) {
				rsize = 0;
			} else {
				if (!modep->opos)
					pd_dfp_enter_mode(port, TCPC_TX_SOP, 0,
							0);

				if (modep->opos) {
					rsize = modep->fx->status(port,
								  payload);
					payload[0] |= PD_VDO_OPOS(modep->opos);
				}
			}
			break;
		case CMD_DP_STATUS:
			/* DP status response & UFP's DP attention have same
			   payload */
			dfp_consume_attention(port, payload);
			if (modep && modep->opos)
				rsize = modep->fx->config(port, payload);
			else
				rsize = 0;
			break;
		case CMD_DP_CONFIG:
			if (modep && modep->opos && modep->fx->post_config)
				modep->fx->post_config(port);
			/* no response after DFPs ack */
			rsize = 0;
			break;
		case CMD_EXIT_MODE:
			/* no response after DFPs ack */
			rsize = 0;
			break;
#endif
		case CMD_ATTENTION:
			/* no response after DFPs ack */
			rsize = 0;
			break;
		default:
			CPRINTF("ERR:CMD:%d\n", cmd);
			rsize = 0;
		}

		payload[0] |= VDO_CMDT(CMDT_INIT);
		payload[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	} else if (cmd_type == CMDT_RSP_BUSY) {
		switch (cmd) {
		case CMD_DISCOVER_IDENT:
		case CMD_DISCOVER_SVID:
		case CMD_DISCOVER_MODES:
			/* resend if its discovery */
			rsize = 1;
			break;
		case CMD_ENTER_MODE:
			/* Error */
			CPRINTF("ERR:ENTBUSY\n");
			rsize = 0;
			break;
		case CMD_EXIT_MODE:
			rsize = 0;
			break;
		default:
			rsize = 0;
		}
	} else if (cmd_type == CMDT_RSP_NAK) {
		/* Passive cable Nacked for Discover SVID */
		if (cmd == CMD_DISCOVER_SVID && is_tbt_compat_enabled(port) &&
		    sop == TCPC_TX_SOP_PRIME &&
		    get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE) {
			limit_tbt_cable_speed(port);
			rsize = dfp_discover_modes(port, payload);
		} else {
			rsize = 0;
		}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
	} else {
		CPRINTF("ERR:CMDT:%d\n", cmd);
		/* do not answer */
		rsize = 0;
	}
	return rsize;
}

#else

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload,
		uint32_t head, enum tcpm_transmit_type *rtype)
{
	return 0;
}

#endif /* CONFIG_USB_PD_ALT_MODE */

#define FW_RW_END (CONFIG_EC_WRITABLE_STORAGE_OFF + \
		   CONFIG_RW_STORAGE_OFF + CONFIG_RW_SIZE)

uint8_t *flash_hash_rw(void)
{
	static struct sha256_ctx ctx;

	/* re-calculate RW hash when changed as its time consuming */
	if (rw_flash_changed) {
		rw_flash_changed = 0;
		SHA256_init(&ctx);
		SHA256_update(&ctx, (void *)CONFIG_PROGRAM_MEMORY_BASE +
			      CONFIG_RW_MEM_OFF,
			      CONFIG_RW_SIZE - RSANUMBYTES);
		return SHA256_final(&ctx);
	} else {
		return ctx.buf;
	}
}

void pd_get_info(uint32_t *info_data)
{
	void *rw_hash = flash_hash_rw();

	/* copy first 20 bytes of RW hash */
	memcpy(info_data, rw_hash, 5 * sizeof(uint32_t));
	/* copy other info into data msg */
#if defined(CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR) && \
	defined(CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR)
	info_data[5] = VDO_INFO(CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR,
				CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR,
				ver_get_num_commits(system_get_image_copy()),
				(system_get_image_copy() != EC_IMAGE_RO));
#else
	info_data[5] = 0;
#endif
}

int pd_custom_flash_vdm(int port, int cnt, uint32_t *payload)
{
	static int flash_offset;
	int rsize = 1; /* default is just VDM header returned */

	switch (PD_VDO_CMD(payload[0])) {
	case VDO_CMD_VERSION:
		memcpy(payload + 1, &current_image_data.version, 24);
		rsize = 7;
		break;
	case VDO_CMD_REBOOT:
		/* ensure the power supply is in a safe state */
		pd_power_supply_reset(0);
		system_reset(0);
		break;
	case VDO_CMD_READ_INFO:
		/* copy info into response */
		pd_get_info(payload + 1);
		rsize = 7;
		break;
	case VDO_CMD_FLASH_ERASE:
		/* do not kill the code under our feet */
		if (system_get_image_copy() != EC_IMAGE_RO)
			break;
		pd_log_event(PD_EVENT_ACC_RW_ERASE, 0, 0, NULL);
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
			       CONFIG_RW_STORAGE_OFF;
		flash_physical_erase(CONFIG_EC_WRITABLE_STORAGE_OFF +
				     CONFIG_RW_STORAGE_OFF, CONFIG_RW_SIZE);
		rw_flash_changed = 1;
		break;
	case VDO_CMD_FLASH_WRITE:
		/* do not kill the code under our feet */
		if ((system_get_image_copy() != EC_IMAGE_RO) ||
		    (flash_offset < CONFIG_EC_WRITABLE_STORAGE_OFF +
				    CONFIG_RW_STORAGE_OFF))
			break;
		flash_physical_write(flash_offset, 4*(cnt - 1),
				     (const char *)(payload+1));
		flash_offset += 4*(cnt - 1);
		rw_flash_changed = 1;
		break;
	case VDO_CMD_ERASE_SIG:
		/* this is not touching the code area */
		{
			uint32_t zero = 0;
			int offset;
			/* zeroes the area containing the RSA signature */
			for (offset = FW_RW_END - RSANUMBYTES;
			     offset < FW_RW_END; offset += 4)
				flash_physical_write(offset, 4,
						     (const char *)&zero);
		}
		break;
	default:
		/* Unknown : do not answer */
		return 0;
	}
	return rsize;
}
