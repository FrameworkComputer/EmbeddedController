/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * USB4 mode support
 * Refer USB Type-C Cable and Connector Specification Release 2.0 Section 5 and
 * USB Power Delivery Specification Revision 3.0, Version 2.0 Section 6.4.8
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 17

#include "compile_time_macros.h"
#include "console.h"
#include "tcpm/tcpm.h"
#include "typec_control.h"
#include "usb_common.h"
#include "usb_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tbt_alt_mode.h"
#include "usbc_ppc.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 41

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
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

static enum usb_rev30_ss
tbt_to_usb4_speed(int port, enum tbt_compat_cable_speed tbt_speed)
{
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);

	if (pd_get_rev(port, TCPCI_MSG_SOP_PRIME) == PD_REV30) {
		if (tbt_speed == TBT_SS_TBT_GEN3)
			return disc->identity.product_t1.p_rev30.ss;
		else
			return USB_R30_SS_U32_U40_GEN2;
	} else {
		if (tbt_speed == TBT_SS_TBT_GEN3)
			return USB_R30_SS_U40_GEN3;
		return USB_R30_SS_U32_U40_GEN2;
	}
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
	/* If TBT mode is active, leave safe state for mode exit VDMs */
	if (!tbt_is_active(port))
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

	if (prl_get_rev(port, TCPCI_MSG_SOP) < PD_REV30)
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
		const struct pd_discovery *disc_sop_prime =
			pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);

		if (pd_get_vdo_ver(port, TCPCI_MSG_SOP_PRIME) >= SVDM_VER_2_0 &&
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
			 * For VDM version < 2.0 or VDO version < 1.3, do not
			 * enter USB4 mode if the cable - doesn't support modal
			 * operation or doesn't support Intel SVID or doesn't
			 * have rounded support.
			 */
		} else {
			const struct pd_discovery *disc =
				pd_get_am_discovery(port, TCPCI_MSG_SOP);
			union tbt_mode_resp_cable cable_mode_resp = {
				.raw_value = pd_get_tbt_mode_vdo(
					port, TCPCI_MSG_SOP_PRIME)
			};

			if (!disc->identity.idh.modal_support ||
			    !pd_is_mode_discovered_for_svid(
				    port, TCPCI_MSG_SOP_PRIME, USB_VID_INTEL) ||
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
	const struct pd_discovery *disc;

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
		typec_set_sbu(port, true);

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
	const struct pd_discovery *disc_sop_prime;

	switch (usb4_state[port]) {
	case USB4_START:
		disc_sop_prime = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
		/*
		 * Ref: Tiger Lake Platform PD Controller Interface Requirements
		 * for Integrated USBC, section A.2.2: USB4 as DFP.
		 * Enter safe mode before sending Enter USB SOP/SOP'/SOP''
		 */
		usb_mux_set_safe_mode(port);

		if (pd_get_vdo_ver(port, TCPCI_MSG_SOP_PRIME) < SVDM_VER_2_0 ||
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

/*
 * For Cable rev 3.0: USB4 cable speed is set according to speed supported by
 * the port and the response received from the cable, whichever is least.
 *
 * For Cable rev 2.0: If get_tbt_cable_speed() is less than
 * TBT_SS_U31_GEN1, return USB_R30_SS_U2_ONLY speed since the board
 * doesn't support superspeed else the USB4 cable speed is set according to
 * the cable response.
 */
enum usb_rev30_ss get_usb4_cable_speed(int port)
{
	enum tbt_compat_cable_speed tbt_speed = get_tbt_cable_speed(port);
	enum usb_rev30_ss max_usb4_speed;

	if (tbt_speed < TBT_SS_U31_GEN1)
		return USB_R30_SS_U2_ONLY;

	/*
	 * Converting Thunderbolt-Compatible board speed to equivalent USB4
	 * speed.
	 */
	max_usb4_speed = tbt_to_usb4_speed(port, tbt_speed);

	if ((get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE) &&
	    pd_get_rev(port, TCPCI_MSG_SOP_PRIME) == PD_REV30) {
		const struct pd_discovery *disc =
			pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
		union active_cable_vdo1_rev30 a_rev30 =
			disc->identity.product_t1.a_rev30;

		if (a_rev30.vdo_ver >= VDO_VERSION_1_3) {
			return max_usb4_speed < a_rev30.ss ? max_usb4_speed :
							     a_rev30.ss;
		}
	}

	return max_usb4_speed;
}

uint32_t get_enter_usb_msg_payload(int port)
{
	/*
	 * Ref: USB Power Delivery Specification Revision 3.0, Version 2.0
	 * Table 6-47 Enter_USB Data Object
	 */
	union enter_usb_data_obj eudo;
	const struct pd_discovery *disc;
	union tbt_mode_resp_cable cable_mode_resp;

	if (!IS_ENABLED(CONFIG_USB_PD_USB4))
		return 0;

	disc = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
	eudo.mode = USB_PD_40;
	eudo.usb4_drd_cap = IS_ENABLED(CONFIG_USB_PD_USB4_DRD);
	eudo.usb3_drd_cap = IS_ENABLED(CONFIG_USB_PD_USB32_DRD);
	eudo.cable_speed = get_usb4_cable_speed(port);

	if (disc->identity.idh.product_type == IDH_PTYPE_ACABLE) {
		if (pd_get_rev(port, TCPCI_MSG_SOP_PRIME) == PD_REV30) {
			enum retimer_active_element active_element =
				disc->identity.product_t2.a2_rev30.active_elem;
			eudo.cable_type = active_element == ACTIVE_RETIMER ?
						  CABLE_TYPE_ACTIVE_RETIMER :
						  CABLE_TYPE_ACTIVE_REDRIVER;
		} else {
			cable_mode_resp.raw_value =
				pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME);

			eudo.cable_type = cable_mode_resp.retimer_type ==
							  USB_RETIMER ?
						  CABLE_TYPE_ACTIVE_RETIMER :
						  CABLE_TYPE_ACTIVE_REDRIVER;
		}
	} else {
		cable_mode_resp.raw_value =
			pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME);

		eudo.cable_type = cable_mode_resp.tbt_active_passive ==
						  TBT_CABLE_ACTIVE ?
					  CABLE_TYPE_ACTIVE_REDRIVER :
					  CABLE_TYPE_PASSIVE;
	}

	switch (disc->identity.product_t1.p_rev20.vbus_cur) {
	case USB_VBUS_CUR_3A:
		eudo.cable_current = USB4_CABLE_CURRENT_3A;
		break;
	case USB_VBUS_CUR_5A:
		eudo.cable_current = USB4_CABLE_CURRENT_5A;
		break;
	default:
		eudo.cable_current = USB4_CABLE_CURRENT_INVALID;
		break;
	}
	eudo.pcie_supported = IS_ENABLED(CONFIG_USB_PD_PCIE_TUNNELING);
	eudo.dp_supported = IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP);
	eudo.tbt_supported = IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE);
	eudo.host_present = 1;

	return eudo.raw_value;
}
