/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Host commands shared across multiple USB-PD implementations
 */

#include "atomic.h"
#include "ec_commands.h"
#include "host_command.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"

#include <string.h>

__overridable enum ec_pd_port_location board_get_pd_port_location(int port)
{
	(void)port;
	return EC_PD_PORT_LOCATION_UNKNOWN;
}

static enum ec_status hc_get_pd_port_caps(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_pd_port_caps *p = args->params;
	struct ec_response_get_pd_port_caps *r = args->response;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	/* Power Role */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE))
		r->pd_power_role_cap = EC_PD_POWER_ROLE_DUAL;
	else
		r->pd_power_role_cap = EC_PD_POWER_ROLE_SINK;

	/* Try-Power Role */
	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		r->pd_try_power_role_cap = EC_PD_TRY_POWER_ROLE_SOURCE;
	else
		r->pd_try_power_role_cap = EC_PD_TRY_POWER_ROLE_NONE;

	if (IS_ENABLED(CONFIG_USB_VPD) || IS_ENABLED(CONFIG_USB_CTVPD))
		r->pd_data_role_cap = EC_PD_DATA_ROLE_UFP;
	else
		r->pd_data_role_cap = EC_PD_DATA_ROLE_DUAL;

	/* Allow boards to override the locations from UNKNOWN if desired */
	r->pd_port_location = board_get_pd_port_location(p->port);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PD_PORT_CAPS, hc_get_pd_port_caps,
		     EC_VER_MASK(0));

#ifdef CONFIG_COMMON_RUNTIME
static const enum pd_dual_role_states dual_role_map[USB_PD_CTRL_ROLE_COUNT] = {
	[USB_PD_CTRL_ROLE_TOGGLE_ON] = PD_DRP_TOGGLE_ON,
	[USB_PD_CTRL_ROLE_TOGGLE_OFF] = PD_DRP_TOGGLE_OFF,
	[USB_PD_CTRL_ROLE_FORCE_SINK] = PD_DRP_FORCE_SINK,
	[USB_PD_CTRL_ROLE_FORCE_SOURCE] = PD_DRP_FORCE_SOURCE,
	[USB_PD_CTRL_ROLE_FREEZE] = PD_DRP_FREEZE,
};

static const mux_state_t typec_mux_map[USB_PD_CTRL_MUX_COUNT] = {
	[USB_PD_CTRL_MUX_NONE] = USB_PD_MUX_NONE,
	[USB_PD_CTRL_MUX_USB] = USB_PD_MUX_USB_ENABLED,
	[USB_PD_CTRL_MUX_AUTO] = USB_PD_MUX_DP_ENABLED,
	[USB_PD_CTRL_MUX_DP] = USB_PD_MUX_DP_ENABLED,
	[USB_PD_CTRL_MUX_DOCK] = USB_PD_MUX_DOCK,
};

static uint8_t pd_get_role_flags(int port)
{
	return (pd_get_power_role(port) == PD_ROLE_SOURCE ?
			PD_CTRL_RESP_ROLE_POWER :
			0) |
	       (pd_get_data_role(port) == PD_ROLE_DFP ? PD_CTRL_RESP_ROLE_DATA :
							0) |
	       (pd_get_vconn_state(port) ? PD_CTRL_RESP_ROLE_VCONN : 0) |
	       (pd_get_partner_dual_role_power(port) ?
			PD_CTRL_RESP_ROLE_DR_POWER :
			0) |
	       (pd_get_partner_data_swap_capable(port) ?
			PD_CTRL_RESP_ROLE_DR_DATA :
			0) |
	       (pd_get_partner_usb_comm_capable(port) ?
			PD_CTRL_RESP_ROLE_USB_COMM :
			0) |
	       (pd_get_partner_unconstr_power(port) ?
			PD_CTRL_RESP_ROLE_UNCONSTRAINED :
			0);
}

static enum ec_status hc_usb_pd_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_control *p = args->params;
	struct ec_response_usb_pd_control_v2 *r_v2 = args->response;
	struct ec_response_usb_pd_control_v1 *r_v1 = args->response;
	struct ec_response_usb_pd_control *r = args->response;
	const char *task_state_name;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (p->role >= USB_PD_CTRL_ROLE_COUNT ||
	    p->mux >= USB_PD_CTRL_MUX_COUNT)
		return EC_RES_INVALID_PARAM;

	if (p->role != USB_PD_CTRL_ROLE_NO_CHANGE) {
		if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE))
			pd_set_dual_role(p->port, dual_role_map[p->role]);
		else
			return EC_RES_INVALID_PARAM;
	}

	if (IS_ENABLED(CONFIG_USBC_SS_MUX) &&
	    p->mux != USB_PD_CTRL_MUX_NO_CHANGE)
		usb_mux_set(p->port, typec_mux_map[p->mux],
			    typec_mux_map[p->mux] == USB_PD_MUX_NONE ?
				    USB_SWITCH_DISCONNECT :
				    USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(p->port)));

	if (p->swap == USB_PD_CTRL_SWAP_DATA) {
		pd_request_data_swap(p->port);
	} else if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE)) {
		if (p->swap == USB_PD_CTRL_SWAP_POWER)
			pd_request_power_swap(p->port);
		else if (IS_ENABLED(CONFIG_USBC_VCONN_SWAP) &&
			 p->swap == USB_PD_CTRL_SWAP_VCONN)
			pd_request_vconn_swap(p->port);
	}

	switch (args->version) {
	case 0:
		r->enabled = pd_comm_is_enabled(p->port);
		r->polarity = pd_get_polarity(p->port);
		r->role = pd_get_power_role(p->port);
		r->state = pd_get_task_state(p->port);
		args->response_size = sizeof(*r);
		break;
	case 1:
	case 2:
		r_v2->enabled = (pd_comm_is_enabled(p->port) ?
					 PD_CTRL_RESP_ENABLED_COMMS :
					 0) |
				(pd_is_connected(p->port) ?
					 PD_CTRL_RESP_ENABLED_CONNECTED :
					 0) |
				(pd_capable(p->port) ?
					 PD_CTRL_RESP_ENABLED_PD_CAPABLE :
					 0);
		r_v2->role = pd_get_role_flags(p->port);
		r_v2->polarity = pd_get_polarity(p->port);

		r_v2->cc_state = pd_get_task_cc_state(p->port);
		task_state_name = pd_get_task_state_name(p->port);
		if (task_state_name)
			strzcpy(r_v2->state, task_state_name,
				sizeof(r_v2->state));
		else
			r_v2->state[0] = '\0';

		r_v2->control_flags = get_pd_control_flags(p->port);

		r_v2->dp_mode = get_dp_pin_mode(p->port);

		if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)) {
			r_v2->cable_speed = get_tbt_cable_speed(p->port);
			r_v2->cable_gen = get_tbt_rounded_support(p->port);
		}

		if (args->version == 1)
			args->response_size = sizeof(*r_v1);
		else
			args->response_size = sizeof(*r_v2);

		break;
	default:
		return EC_RES_INVALID_PARAM;
	}
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_CONTROL, hc_usb_pd_control,
		     EC_VER_MASK(0) | EC_VER_MASK(1) | EC_VER_MASK(2));
#endif /* CONFIG_COMMON_RUNTIME */

#if defined(CONFIG_HOSTCMD_TYPEC_STATUS) && !defined(CONFIG_USB_PD_TCPMV1)
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

#if !defined(CONFIG_USB_PD_TCPM_STUB)
/*
 * PD host event status for host command
 * Note: this variable must be aligned on 4-byte boundary because we pass the
 * address to atomic_ functions which use assembly to access them.
 */
static atomic_t pd_host_event_status __aligned(4);

test_mockable void pd_send_host_event(int mask)
{
	/* mask must be set */
	if (!mask)
		return;

	atomic_or(&pd_host_event_status, mask);
	/* interrupt the AP */
	host_set_single_event(EC_HOST_EVENT_PD_MCU);
}

static enum ec_status
hc_pd_host_event_status(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_status *r = args->response;

	/* Read and clear the host event status to return to AP */
	r->status = atomic_clear(&pd_host_event_status);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_HOST_EVENT_STATUS, hc_pd_host_event_status,
		     EC_VER_MASK(0));
#endif /* ! CONFIG_USB_PD_TCPM_STUB */

#ifdef CONFIG_HOSTCMD_TYPEC_CONTROL
static enum ec_status hc_typec_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_typec_control *p = args->params;
	mux_state_t mode;
	uint32_t data[VDO_MAX_SIZE];
	enum tcpci_msg_type tx_type;

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
	case TYPEC_CONTROL_COMMAND_SEND_VDM_REQ:
		if (!IS_ENABLED(CONFIG_USB_PD_VDM_AP_CONTROL))
			return EC_RES_INVALID_PARAM;

		if (p->vdm_req_params.vdm_data_objects <= 0 ||
		    p->vdm_req_params.vdm_data_objects > VDO_MAX_SIZE)
			return EC_RES_INVALID_PARAM;

		memcpy(data, p->vdm_req_params.vdm_data,
		       sizeof(uint32_t) * p->vdm_req_params.vdm_data_objects);

		switch (p->vdm_req_params.partner_type) {
		case TYPEC_PARTNER_SOP:
			tx_type = TCPCI_MSG_SOP;
			break;
		case TYPEC_PARTNER_SOP_PRIME:
			tx_type = TCPCI_MSG_SOP_PRIME;
			break;
		case TYPEC_PARTNER_SOP_PRIME_PRIME:
			tx_type = TCPCI_MSG_SOP_PRIME_PRIME;
			break;
		default:
			return EC_RES_INVALID_PARAM;
		}

		return pd_request_vdm(p->port, data,
				      p->vdm_req_params.vdm_data_objects,
				      tx_type);
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TYPEC_CONTROL, hc_typec_control, EC_VER_MASK(0));
#endif /* CONFIG_HOSTCMD_TYPEC_CONTROL */

#if defined(CONFIG_USB_PD_ALT_MODE_DFP) || \
	defined(CONFIG_PLATFORM_EC_USB_PD_CONTROLLER)
static enum ec_status hc_remote_pd_discovery(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_info_request *p = args->params;
	struct ec_params_usb_pd_discovery_entry *r = args->response;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	r->vid = pd_get_identity_vid(p->port);
	r->ptype = pd_get_product_type(p->port);

	/* pid only included if vid is assigned */
	if (r->vid)
		r->pid = pd_get_identity_pid(p->port);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_DISCOVERY, hc_remote_pd_discovery,
		     EC_VER_MASK(0));
#endif /* CONFIG_USB_PD_ALT_MODE_DFP || CONFIG_PLATFORM_EC_USB_PD_CONTROLLER \
	*/
