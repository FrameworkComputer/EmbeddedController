/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * USB4 mode support
 * Refer USB Type-C Cable and Connector Specification Release 2.0 Section 5 and
 * USB Power Delivery Specification Revision 3.0, Version 2.0 Section 6.4.8
 */

#include <stdbool.h>
#include <stdint.h>
#include "compile_time_macros.h"
#include "console.h"
#include "tcpm/tcpm.h"
#include "usb_common.h"
#include "usb_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usbc_ppc.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

enum usb4_mode_status {
	USB4_MODE_FAILURE,
	USB4_MODE_SUCCESS,
};

enum usb4_states {
	USB4_START,
	USB4_ENTER_SOP,
	USB4_ENTER_SOP_PRIME,
	USB4_ENTER_SOP_PRIME_PRIME,
	USB4_ACTIVE,
	USB4_INACTIVE,
	USB4_STATE_COUNT,
};

/*
 * USB4 PD flow:
 *
 *                            Cable type
 *                                 |
 *            |-------- Passive ---|---- Active -----|
 *            |                                      |
 *      USB Highest Speed         Structured VDM version
 *            |                   (cable revision)-- <2.0---->|
 *    --------|--------|------|       |                       |
 *    |       |        |      |       >=2.0                   |
 *  >=Gen3   Gen2    Gen1  USB2.0     |                       |
 *    |       |        |      |   VDO version--- <1.3 ---> Modal op? -- N --|
 * Enter USB  |        |      |   (B21:23 of                  |             |
 * SOP  with  |        |      |    Discover ID SOP'-          y             |
 * Gen3 cable |        |    Skip   Active cable VDO1)         |             |
 * speed      |        |    USB4      |                    TBT SVID? -- N --|
 *            |        |    mode      >=1.3                   |             |
 *    Is modal op?     |    entry     |                       y             |
 *            |        |            Cable USB4  - N           |             |
 *            y        |            support?      |       Gen4 cable? - N - Skip
 *            |        |               |      Skip USB4       |             USB4
 *    Is TBT SVID? -N- Enter           |      mode entry      |             mode
 *            |       USB4 SOP         |                      |            entry
 *            y       with Gen2        y                      |
 *            |       cable speed      |                      |
 *            |                        |                      |
 *    Is Discover mode                 |                      |
 *    SOP' B25? - N - Enter      Enter USB4 mode              |
 *            |     USB4 SOP     (SOP, SOP', SOP'')           |
 *            |     with speed                                |
 *            y     from TBT mode                             |
 *            |     SOP' VDO                                  |
 *            |                           |<-- NAK -- Enter mode TBT SOP'<---|
 * |---->Enter TBT SOP'-------NAK------>| |                   |              |
 * |          |                         | |                  ACK             |
 * |         ACK                        | |                   |              |
 * |          |                         | |<-- NAK -- Enter mode TBT SOP''   |
 * |     Enter USB4 SOP                 | |                   |              |
 * |     with speed from         Exit TBT mode SOP           ACK             |
 * |     TBT mode SOP' VDO              | |                   |              |
 * |                                  ACK/NAK          Enter USB4 SOP        |
 * |                                    | |            with speed from       |
 * |                             Exit TBT mode SOP''   TBT mode SOP' VDO     |
 * |                                    | |                                  |
 * |                                  ACK/NAK                                |
 * |                                    | |                                  |
 * |                             Exit TBT mode SOP'                          |
 * |                                    | |                                  |
 * |                                   ACK/NAK                               |
 * |                                    | |                                  |
 * |---- N ----Retry done? -------------| |--------Retry done? ---- N -------|
 *                  |                                   |
 *                  y                                   y
 *                  |                                   |
 *           Skip USB4 mode entry                 Skip USB4 mode entry
 */

static enum usb4_states usb4_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static void usb4_debug_prints(int port, enum usb4_mode_status usb4_status)
{
	CPRINTS("C%d: USB4: State:%d Status:%d", port, usb4_state[port],
		usb4_status);
}

bool enter_usb_entry_is_done(int port)
{
	return usb4_state[port] == USB4_ACTIVE ||
		usb4_state[port] == USB4_INACTIVE;
}

void usb4_exit_mode_request(int port)
{
	usb4_state[port] = USB4_START;
	usb_mux_set_safe_mode_exit(port);
	set_usb_mux_with_current_data_role(port);
}

void enter_usb_init(int port)
{
	usb4_state[port] = USB4_START;
}

void enter_usb_failed(int port)
{
	/*
	 * Since Enter USB sets the mux state to SAFE mode, fall back
	 * to USB mode on receiving a NAK.
	 */
	usb_mux_set(port, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT,
		    polarity_rm_dts(pd_get_polarity(port)));

	usb4_debug_prints(port, USB4_MODE_FAILURE);
	usb4_state[port] = USB4_INACTIVE;
}

static bool enter_usb_response_valid(int port, enum tcpci_msg_type type)
{
	/*
	 * Check for an unexpected response.
	 */
	if (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE &&
	     type != TCPCI_MSG_SOP) {
		enter_usb_failed(port);
		return false;
	}
	return true;
}

bool enter_usb_port_partner_is_capable(int port)
{
	const struct pd_discovery *disc =
			pd_get_am_discovery(port, TCPCI_MSG_SOP);

	if (usb4_state[port] == USB4_INACTIVE)
		return false;

	if (!PD_PRODUCT_IS_USB4(disc->identity.product_t1.raw_value))
		return false;

	return true;
}

bool enter_usb_cable_is_capable(int port)
{
	if (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE) {
		if (get_usb4_cable_speed(port) < USB_R30_SS_U32_U40_GEN1)
			return false;
	} else if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE) {
		struct pd_discovery *disc_sop_prime =
			pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);

		if (pd_get_vdo_ver(port, TCPCI_MSG_SOP_PRIME) >= VDM_VER20 &&
		    disc_sop_prime->identity.product_t1.a_rev30.vdo_ver >=
							VDO_VERSION_1_3) {
			union active_cable_vdo2_rev30 a2_rev30 =
				disc_sop_prime->identity.product_t2.a2_rev30;
			/*
			 * For VDM version >= 2.0 and VD0 version is >= 1.3,
			 * do not enter USB4 mode if the cable isn't USB4
			 * capable.
			 */
			if (a2_rev30.usb_40_support == USB4_NOT_SUPPORTED)
				return false;
		/*
		 * For VDM version < 2.0 or VDO version < 1.3, do not enter USB4
		 * mode if the cable -
		 * doesn't support modal operation or
		 * doesn't support Intel SVID or
		 * doesn't have rounded support.
		 */
		} else {
			const struct pd_discovery *disc =
				pd_get_am_discovery(port, TCPCI_MSG_SOP);
			union tbt_mode_resp_cable cable_mode_resp = {
				.raw_value = pd_get_tbt_mode_vdo(port,
							TCPCI_MSG_SOP_PRIME) };

			if (!disc->identity.idh.modal_support ||
			   !pd_is_mode_discovered_for_svid(port,
					TCPCI_MSG_SOP_PRIME, USB_VID_INTEL) ||
			    cable_mode_resp.tbt_rounded !=
					TBT_GEN3_GEN4_ROUNDED_NON_ROUNDED)
				return false;
		}
	} else {
		/* Not Emark cable */
		return false;
	}

	return true;
}

void enter_usb_accepted(int port, enum tcpci_msg_type type)
{
	struct pd_discovery *disc;

	if (!enter_usb_response_valid(port, type))
		return;

	switch (usb4_state[port]) {
	case USB4_ENTER_SOP_PRIME:
		disc = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
		if (disc->identity.product_t1.a_rev20.sop_p_p)
			usb4_state[port] = USB4_ENTER_SOP_PRIME_PRIME;
		else
			usb4_state[port] = USB4_ENTER_SOP;
		break;
	case USB4_ENTER_SOP_PRIME_PRIME:
		usb4_state[port] = USB4_ENTER_SOP;
		break;
	case USB4_ENTER_SOP:
		/* Connect the SBU and USB lines to the connector */
		if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
			ppc_set_sbu(port, 1);

		usb4_state[port] = USB4_ACTIVE;

		/* Set usb mux to USB4 mode */
		usb_mux_set(port, USB_PD_MUX_USB4_ENABLED, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));

		usb4_debug_prints(port, USB4_MODE_SUCCESS);
		break;
	case USB4_ACTIVE:
		break;
	default:
		enter_usb_failed(port);
	}
}

void enter_usb_rejected(int port, enum tcpci_msg_type type)
{
	if (!enter_usb_response_valid(port, type) ||
	    usb4_state[port] == USB4_ACTIVE)
		return;

	enter_usb_failed(port);
}

uint32_t enter_usb_setup_next_msg(int port, enum tcpci_msg_type *type)
{
	struct pd_discovery *disc_sop_prime;

	switch (usb4_state[port]) {
	case USB4_START:
		disc_sop_prime = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
		/*
		 * Ref: Tiger Lake Platform PD Controller Interface Requirements
		 * for Integrated USBC, section A.2.2: USB4 as DFP.
		 * Enter safe mode before sending Enter USB SOP/SOP'/SOP''
		 * TODO (b/156749387): Remove once data reset feature is in
		 * place.
		 */
		usb_mux_set_safe_mode(port);

		if (pd_get_vdo_ver(port, TCPCI_MSG_SOP_PRIME) < VDM_VER20 ||
		    disc_sop_prime->identity.product_t1.a_rev30.vdo_ver <
							VDO_VERSION_1_3 ||
		    get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE) {
			usb4_state[port] = USB4_ENTER_SOP;
		} else {
			usb4_state[port] = USB4_ENTER_SOP_PRIME;
			*type = TCPCI_MSG_SOP_PRIME;
		}
		break;
	case USB4_ENTER_SOP_PRIME:
		*type = TCPCI_MSG_SOP_PRIME;
		break;
	case USB4_ENTER_SOP_PRIME_PRIME:
		*type = TCPCI_MSG_SOP_PRIME_PRIME;
		break;
	case USB4_ENTER_SOP:
		*type = TCPCI_MSG_SOP;
		break;
	case USB4_ACTIVE:
		return -1;
	default:
		return 0;
	}
	return get_enter_usb_msg_payload(port);
}
