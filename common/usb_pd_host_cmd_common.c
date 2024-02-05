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

/*
 * Combines the following information into a single byte
 * Bit 0: Active/Passive cable
 * Bit 1: Optical/Non-optical cable
 * Bit 2: Legacy Thunderbolt adapter
 * Bit 3: Active Link Uni-Direction/Bi-Direction
 * Bit 4: Retimer/Rediriver cable
 */
static uint8_t get_pd_control_flags(int port)
{
	union tbt_mode_resp_cable cable_resp;
	union tbt_mode_resp_device device_resp;
	uint8_t control_flags = 0;

	if (!IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP) ||
	    !IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE))
		return 0;

	cable_resp.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME);
	device_resp.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP);

	/*
	 * Ref: USB Type-C Cable and Connector Specification
	 * Table F-11 TBT3 Cable Discover Mode VDO Responses
	 * For Passive cables, Active Cable Plug link training is set to 0
	 */
	control_flags |= (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE ||
			  cable_resp.tbt_active_passive == TBT_CABLE_ACTIVE) ?
				 USB_PD_CTRL_ACTIVE_CABLE :
				 0;
	control_flags |= cable_resp.tbt_cable == TBT_CABLE_OPTICAL ?
				 USB_PD_CTRL_OPTICAL_CABLE :
				 0;
	control_flags |= device_resp.tbt_adapter == TBT_ADAPTER_TBT2_LEGACY ?
				 USB_PD_CTRL_TBT_LEGACY_ADAPTER :
				 0;
	control_flags |= cable_resp.lsrx_comm == UNIDIR_LSRX_COMM ?
				 USB_PD_CTRL_ACTIVE_LINK_UNIDIR :
				 0;
	control_flags |= cable_resp.retimer_type == USB_RETIMER ?
				 USB_PD_CTRL_RETIMER_CABLE :
				 0;
	return control_flags;
}

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
