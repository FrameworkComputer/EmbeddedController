/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Host commands for USB-PD module.
 */

#include "atomic.h"
#include "battery.h"
#include "charge_manager.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "tcpm/tcpm.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#include <string.h>
#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif /* CONFIG_COMMON_RUNTIME */

#ifdef HAS_TASK_HOSTCMD

static enum ec_status hc_pd_ports(struct host_cmd_handler_args *args)
{
	struct ec_response_usb_pd_ports *r = args->response;

	r->num_ports = board_get_usb_pd_port_count();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_PORTS, hc_pd_ports, EC_VER_MASK(0));

#if defined(CONFIG_HOSTCMD_RWHASHPD) && defined(CONFIG_COMMON_RUNTIME)
static enum ec_status
hc_remote_rw_hash_entry(struct host_cmd_handler_args *args)
{
	int i, idx = 0, found = 0;
	const struct ec_params_usb_pd_rw_hash_entry *p = args->params;
	static int rw_hash_next_idx;

	if (!p->dev_id)
		return EC_RES_INVALID_PARAM;

	for (i = 0; i < RW_HASH_ENTRIES; i++) {
		if (p->dev_id == rw_hash_table[i].dev_id) {
			idx = i;
			found = 1;
			break;
		}
	}

	if (!found) {
		idx = rw_hash_next_idx;
		rw_hash_next_idx = rw_hash_next_idx + 1;
		if (rw_hash_next_idx == RW_HASH_ENTRIES)
			rw_hash_next_idx = 0;
	}
	memcpy(&rw_hash_table[idx], p, sizeof(*p));

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_RW_HASH_ENTRY, hc_remote_rw_hash_entry,
		     EC_VER_MASK(0));
#endif /* CONFIG_HOSTCMD_RWHASHPD && CONFIG_COMMON_RUNTIME */

#if defined(CONFIG_HOSTCMD_PD_CHIP_INFO) && !defined(CONFIG_USB_PD_TCPC)
static enum ec_status hc_remote_pd_chip_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_pd_chip_info *p = args->params;
	struct ec_response_pd_chip_info_v1 info;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (tcpm_get_chip_info(p->port, p->live, &info))
		return EC_RES_ERROR;

	/*
	 * Take advantage of the fact that v0 and v1 structs have the
	 * same layout for v0 data. (v1 just appends data)
	 */
	args->response_size =
		args->version ? sizeof(struct ec_response_pd_chip_info_v1) :
				sizeof(struct ec_response_pd_chip_info);

	memcpy(args->response, &info, args->response_size);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_CHIP_INFO, hc_remote_pd_chip_info,
		     EC_VER_MASK(0) | EC_VER_MASK(1));
#endif /* CONFIG_HOSTCMD_PD_CHIP_INFO && !CONFIG_USB_PD_TCPC */

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
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

#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

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

#ifdef CONFIG_HOSTCMD_PD_CONTROL
static int pd_control_disabled[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Only allow port re-enable in unit tests */
#ifdef TEST_BUILD
void pd_control_port_enable(int port)
{
	pd_control_disabled[port] = 0;
}
#endif /* TEST_BUILD */

static enum ec_status pd_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_pd_control *cmd = args->params;
	int enable = 0;

	if (cmd->chip >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	/* Always allow disable command */
	if (cmd->subcmd == PD_CONTROL_DISABLE) {
		pd_control_disabled[cmd->chip] = 1;
		return EC_RES_SUCCESS;
	}

	if (pd_control_disabled[cmd->chip])
		return EC_RES_ACCESS_DENIED;

	if (cmd->subcmd == PD_SUSPEND) {
		if (!pd_firmware_upgrade_check_power_readiness(cmd->chip))
			return EC_RES_BUSY;
		enable = 0;
	} else if (cmd->subcmd == PD_RESUME) {
		enable = 1;
	} else if (cmd->subcmd == PD_RESET) {
		board_reset_pd_mcu();
	} else if (cmd->subcmd == PD_CHIP_ON && board_set_tcpc_power_mode) {
		board_set_tcpc_power_mode(cmd->chip, 1);
		return EC_RES_SUCCESS;
	} else {
		return EC_RES_INVALID_COMMAND;
	}

	pd_comm_enable(cmd->chip, enable);
	pd_set_suspend(cmd->chip, !enable);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_CONTROL, pd_control, EC_VER_MASK(0));
#endif /* CONFIG_HOSTCMD_PD_CONTROL */

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

#endif /* HAS_TASK_HOSTCMD */
