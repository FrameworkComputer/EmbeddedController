/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Host commands for TCPMv2 USB PD module
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#include <string.h>

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

#ifdef CONFIG_HOSTCMD_TYPEC_DISCOVERY
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
		CPRINTS("[C%d] %s returns EC_RES_BUSY!!", p->port, __func__);
		return EC_RES_BUSY;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TYPEC_DISCOVERY, hc_typec_discovery,
		     EC_VER_MASK(0));
#endif /* CONFIG_HOSTCMD_TYPEC_DISCOVERY */

/* Default to feature unavailable, with boards supporting it overriding */
__overridable enum ec_status
board_set_tbt_ufp_reply(int port, enum typec_tbt_ufp_reply reply)
{
	return EC_RES_UNAVAILABLE;
}

#ifdef CONFIG_HOSTCMD_TYPEC_STATUS
/*
 * Validate ec_response_typec_status_v0's binary compatibility with
 * ec_response_typec_status, which is being deprecated.
 */
BUILD_ASSERT(offsetof(struct ec_response_typec_status_v0,
		      typec_status.sop_prime_revision) ==
	     offsetof(struct ec_response_typec_status, sop_prime_revision));
BUILD_ASSERT(offsetof(struct ec_response_typec_status_v0, source_cap_pdos) ==
	     offsetof(struct ec_response_typec_status, source_cap_pdos));
BUILD_ASSERT(sizeof(struct ec_response_typec_status_v0) ==
	     sizeof(struct ec_response_typec_status));

/*
 * Validate ec_response_typec_status_v0's binary compatibility with
 * ec_response_typec_status_v1 with respect to typec_status.
 */
BUILD_ASSERT(offsetof(struct ec_response_typec_status_v0,
		      typec_status.pd_enabled) ==
	     offsetof(struct ec_response_typec_status_v1,
		      typec_status.pd_enabled));
BUILD_ASSERT(offsetof(struct ec_response_typec_status_v0,
		      typec_status.sop_prime_revision) ==
	     offsetof(struct ec_response_typec_status_v1,
		      typec_status.sop_prime_revision));

static enum ec_status hc_typec_status(struct host_cmd_handler_args *args)
{
	const struct ec_params_typec_status *p = args->params;
	struct ec_response_typec_status_v1 *r1 = args->response;
	struct ec_response_typec_status_v0 *r0 = args->response;
	struct cros_ec_typec_status *cs = &r1->typec_status;
	const char *tc_state_name;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	args->response_size = args->version == 0 ? sizeof(*r0) : sizeof(*r1);

	if (args->response_max < args->response_size)
		return EC_RES_RESPONSE_TOO_BIG;

	cs->pd_enabled = pd_comm_is_enabled(p->port);
	cs->dev_connected = pd_is_connected(p->port);
	cs->sop_connected = pd_capable(p->port);

	cs->power_role = pd_get_power_role(p->port);
	cs->data_role = pd_get_data_role(p->port);
	cs->vconn_role = pd_get_vconn_state(p->port) ? PD_ROLE_VCONN_SRC :
						       PD_ROLE_VCONN_OFF;
	cs->polarity = pd_get_polarity(p->port);
	cs->cc_state = pd_get_task_cc_state(p->port);
	cs->dp_pin = get_dp_pin_mode(p->port);
	cs->mux_state = usb_mux_get(p->port);

	tc_state_name = pd_get_task_state_name(p->port);
	strzcpy(cs->tc_state, tc_state_name, sizeof(cs->tc_state));

	cs->events = pd_get_events(p->port);

	if (pd_get_partner_rmdo(p->port).major_rev != 0) {
		cs->sop_revision =
			PD_STATUS_RMDO_REV_SET_MAJOR(
				pd_get_partner_rmdo(p->port).major_rev) |
			PD_STATUS_RMDO_REV_SET_MINOR(
				pd_get_partner_rmdo(p->port).minor_rev) |
			PD_STATUS_RMDO_VER_SET_MAJOR(
				pd_get_partner_rmdo(p->port).major_ver) |
			PD_STATUS_RMDO_VER_SET_MINOR(
				pd_get_partner_rmdo(p->port).minor_ver);
	} else if (cs->sop_connected) {
		cs->sop_revision = PD_STATUS_REV_SET_MAJOR(
			pd_get_rev(p->port, TCPCI_MSG_SOP));
	} else {
		cs->sop_revision = 0;
	}

	cs->sop_prime_revision =
		pd_get_identity_discovery(p->port, TCPCI_MSG_SOP_PRIME) ==
				PD_DISC_COMPLETE ?
			PD_STATUS_REV_SET_MAJOR(
				pd_get_rev(p->port, TCPCI_MSG_SOP_PRIME)) :
			0;

	if (args->version == 0) {
		cs->source_cap_count = MIN(pd_get_src_cap_cnt(p->port),
					   ARRAY_SIZE(r0->source_cap_pdos));
		memcpy(r0->source_cap_pdos, pd_get_src_caps(p->port),
		       cs->source_cap_count * sizeof(uint32_t));
		cs->sink_cap_count = MIN(pd_get_snk_cap_cnt(p->port),
					 ARRAY_SIZE(r0->sink_cap_pdos));
		memcpy(r0->sink_cap_pdos, pd_get_snk_caps(p->port),
		       cs->sink_cap_count * sizeof(uint32_t));
	} else {
		cs->source_cap_count = MIN(pd_get_src_cap_cnt(p->port),
					   ARRAY_SIZE(r1->source_cap_pdos));
		memcpy(r1->source_cap_pdos, pd_get_src_caps(p->port),
		       cs->source_cap_count * sizeof(uint32_t));
		cs->sink_cap_count = MIN(pd_get_snk_cap_cnt(p->port),
					 ARRAY_SIZE(r1->sink_cap_pdos));
		memcpy(r1->sink_cap_pdos, pd_get_snk_caps(p->port),
		       cs->sink_cap_count * sizeof(uint32_t));
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TYPEC_STATUS, hc_typec_status,
		     EC_VER_MASK(0) | EC_VER_MASK(1));
#endif /* CONFIG_HOSTCMD_TYPEC_STATUS */
