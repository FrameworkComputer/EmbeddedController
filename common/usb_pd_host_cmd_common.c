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
