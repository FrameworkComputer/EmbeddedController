/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Host commands for TCPMv2 USB PD module
 */

#include <string.h>

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* Retrieve all discovery results for the given port and transmit type */
static enum ec_status hc_typec_discovery(struct host_cmd_handler_args *args)
{
	const struct ec_params_typec_discovery *p = args->params;
	struct ec_response_typec_discovery *r = args->response;
	const struct pd_discovery *disc;
	enum tcpci_msg_type type;

	/* Confirm the number of HC VDOs matches our stored VDOs */
	BUILD_ASSERT(sizeof(r->discovery_vdo) == sizeof(union disc_ident_ack));

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (p->partner_type > TYPEC_PARTNER_SOP_PRIME)
		return EC_RES_INVALID_PARAM;

	type = p->partner_type == TYPEC_PARTNER_SOP ? TCPCI_MSG_SOP :
						      TCPCI_MSG_SOP_PRIME;

	/*
	 * Clear out access mask so we can track if tasks have touched data
	 * since read started.
	 */
	pd_discovery_access_clear(p->port, type);

	disc = pd_get_am_discovery_and_notify_access(p->port, type);

	/* Initialize return size to that of discovery with no SVIDs */
	args->response_size = sizeof(*r);

	if (pd_get_identity_discovery(p->port, type) == PD_DISC_COMPLETE) {
		r->identity_count = disc->identity_cnt;
		memcpy(r->discovery_vdo,
		       pd_get_identity_response(p->port, type)->raw_value,
		       sizeof(r->discovery_vdo));
	} else {
		r->identity_count = 0;
		return EC_RES_SUCCESS;
	}

	if (pd_get_modes_discovery(p->port, type) == PD_DISC_COMPLETE) {
		int svid_i;
		int max_resp_svids =
			(args->response_max - args->response_size) /
			sizeof(struct svid_mode_info);

		if (disc->svid_cnt > max_resp_svids) {
			CPRINTS("Warn: SVIDS exceeded HC response");
			r->svid_count = max_resp_svids;
		} else {
			r->svid_count = disc->svid_cnt;
		}

		for (svid_i = 0; svid_i < r->svid_count; svid_i++) {
			r->svids[svid_i].svid = disc->svids[svid_i].svid;
			r->svids[svid_i].mode_count =
				disc->svids[svid_i].mode_cnt;
			memcpy(r->svids[svid_i].mode_vdo,
			       disc->svids[svid_i].mode_vdo,
			       sizeof(r->svids[svid_i].mode_vdo));
			args->response_size += sizeof(struct svid_mode_info);
		}
	} else {
		r->svid_count = 0;
	}

	/*
	 * Verify that another task did not access this data during the duration
	 * of the copy.  If the data was accessed, return BUSY so the AP will
	 * try retrieving again and get the updated data.
	 */
	if (!pd_discovery_access_validate(p->port, type)) {
		CPRINTS("[C%d] %s returns EC_RES_BUSY!!\n", p->port, __func__);
		return EC_RES_BUSY;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TYPEC_DISCOVERY, hc_typec_discovery,
		     EC_VER_MASK(0));

/* Default to feature unavailable, with boards supporting it overriding */
__overridable enum ec_status
board_set_tbt_ufp_reply(int port, enum typec_tbt_ufp_reply reply)
{
	return EC_RES_UNAVAILABLE;
}

static enum ec_status hc_typec_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_typec_control *p = args->params;
	mux_state_t mode;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	switch (p->command) {
	case TYPEC_CONTROL_COMMAND_EXIT_MODES:
		pd_dpm_request(p->port, DPM_REQUEST_EXIT_MODES);
		break;
	case TYPEC_CONTROL_COMMAND_CLEAR_EVENTS:
		pd_clear_events(p->port, p->clear_events_mask);
		break;
	case TYPEC_CONTROL_COMMAND_ENTER_MODE:
		return pd_request_enter_mode(p->port, p->mode_to_enter);
	case TYPEC_CONTROL_COMMAND_TBT_UFP_REPLY:
		return board_set_tbt_ufp_reply(p->port, p->tbt_ufp_reply);
	case TYPEC_CONTROL_COMMAND_USB_MUX_SET:
		/* The EC will fill in polarity, so filter flip out */
		mode = p->mux_params.mux_flags & ~USB_PD_MUX_POLARITY_INVERTED;

		if (!IS_ENABLED(CONFIG_USB_MUX_AP_CONTROL))
			return EC_RES_INVALID_PARAM;

		usb_mux_set_single(p->port, p->mux_params.mux_index, mode,
				   USB_SWITCH_CONNECT,
				   polarity_rm_dts(pd_get_polarity(p->port)));
		return EC_RES_SUCCESS;
	case TYPEC_CONTROL_COMMAND_BIST_SHARE_MODE:
		return pd_set_bist_share_mode(p->bist_share_mode);
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TYPEC_CONTROL, hc_typec_control, EC_VER_MASK(0));

static enum ec_status hc_typec_status(struct host_cmd_handler_args *args)
{
	const struct ec_params_typec_status *p = args->params;
	struct ec_response_typec_status *r = args->response;
	const char *tc_state_name;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (args->response_max < sizeof(*r))
		return EC_RES_RESPONSE_TOO_BIG;

	args->response_size = sizeof(*r);

	r->pd_enabled = pd_comm_is_enabled(p->port);
	r->dev_connected = pd_is_connected(p->port);
	r->sop_connected = pd_capable(p->port);

	r->power_role = pd_get_power_role(p->port);
	r->data_role = pd_get_data_role(p->port);
	r->vconn_role = pd_get_vconn_state(p->port) ? PD_ROLE_VCONN_SRC :
						      PD_ROLE_VCONN_OFF;
	r->polarity = pd_get_polarity(p->port);
	r->cc_state = pd_get_task_cc_state(p->port);
	r->dp_pin = get_dp_pin_mode(p->port);
	r->mux_state = usb_mux_get(p->port);

	tc_state_name = pd_get_task_state_name(p->port);
	strzcpy(r->tc_state, tc_state_name, sizeof(r->tc_state));

	r->events = pd_get_events(p->port);

	r->sop_revision = r->sop_connected ?
				  PD_STATUS_REV_SET_MAJOR(
					  pd_get_rev(p->port, TCPCI_MSG_SOP)) :
				  0;
	r->sop_prime_revision =
		pd_get_identity_discovery(p->port, TCPCI_MSG_SOP_PRIME) ==
				PD_DISC_COMPLETE ?
			PD_STATUS_REV_SET_MAJOR(
				pd_get_rev(p->port, TCPCI_MSG_SOP_PRIME)) :
			0;

	r->source_cap_count = pd_get_src_cap_cnt(p->port);
	memcpy(r->source_cap_pdos, pd_get_src_caps(p->port),
	       r->source_cap_count * sizeof(uint32_t));

	r->sink_cap_count = pd_get_snk_cap_cnt(p->port);
	memcpy(r->sink_cap_pdos, pd_get_snk_caps(p->port),
	       r->sink_cap_count * sizeof(uint32_t));

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TYPEC_STATUS, hc_typec_status, EC_VER_MASK(0));
