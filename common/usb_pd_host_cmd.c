/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Host commands for USB-PD module.
 */

#include <string.h>

#include "atomic.h"
#include "battery.h"
#include "charge_manager.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "tcpm.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usb_pd.h"

#ifdef CONFIG_COMMON_RUNTIME
/*
 * If we are trying to upgrade the TCPC port that is supplying power, then we
 * need to ensure that the battery has enough charge for the upgrade. 100mAh
 * is about 5% of most batteries, and it should be enough charge to get us
 * through the EC jump to RW and PD upgrade.
 */
#define MIN_BATTERY_FOR_TCPC_UPGRADE_MAH 100 /* mAH */

struct ec_params_usb_pd_rw_hash_entry rw_hash_table[RW_HASH_ENTRIES];

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
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
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_PORTS,
		     hc_pd_ports,
		     EC_VER_MASK(0));

#ifdef CONFIG_HOSTCMD_RWHASHPD
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
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_RW_HASH_ENTRY,
		     hc_remote_rw_hash_entry,
		     EC_VER_MASK(0));
#endif /* CONFIG_HOSTCMD_RWHASHPD */

#if defined(CONFIG_EC_CMD_PD_CHIP_INFO) && !defined(CONFIG_USB_PD_TCPC)
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
		args->version ? sizeof(struct ec_response_pd_chip_info_v1)
			      : sizeof(struct ec_response_pd_chip_info);

	memcpy(args->response, &info, args->response_size);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_CHIP_INFO,
		     hc_remote_pd_chip_info,
		     EC_VER_MASK(0) | EC_VER_MASK(1));
#endif /* CONFIG_EC_CMD_PD_CHIP_INFO && !CONFIG_USB_PD_TCPC */

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static enum ec_status hc_remote_pd_set_amode(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_set_mode_request *p = args->params;

	if ((p->port >= board_get_usb_pd_port_count()) ||
	    (!p->svid) || (!p->opos))
		return EC_RES_INVALID_PARAM;

	switch (p->cmd) {
	case PD_EXIT_MODE:
		if (pd_dfp_exit_mode(p->port, TCPC_TX_SOP, p->svid, p->opos))
			pd_send_vdm(p->port, p->svid,
				    CMD_EXIT_MODE | VDO_OPOS(p->opos), NULL, 0);
		else {
			CPRINTF("Failed exit mode\n");
			return EC_RES_ERROR;
		}
		break;
	case PD_ENTER_MODE:
		if (pd_dfp_enter_mode(p->port, TCPC_TX_SOP, p->svid, p->opos))
			pd_send_vdm(p->port, p->svid, CMD_ENTER_MODE |
				    VDO_OPOS(p->opos), NULL, 0);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_SET_AMODE,
		     hc_remote_pd_set_amode,
		     EC_VER_MASK(0));

static enum ec_status hc_remote_pd_discovery(struct host_cmd_handler_args *args)
{
	const uint8_t *port = args->params;
	struct ec_params_usb_pd_discovery_entry *r = args->response;

	if (*port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	r->vid = pd_get_identity_vid(*port);
	r->ptype = pd_get_product_type(*port);

	/* pid only included if vid is assigned */
	if (r->vid)
		r->pid = pd_get_identity_pid(*port);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_DISCOVERY,
		     hc_remote_pd_discovery,
		     EC_VER_MASK(0));

static enum ec_status hc_remote_pd_get_amode(struct host_cmd_handler_args *args)
{
	struct svdm_amode_data *modep;
	const struct ec_params_usb_pd_get_mode_request *p = args->params;
	struct ec_params_usb_pd_get_mode_response *r = args->response;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	/* no more to send */
	/* TODO(b/148528713): Use TCPMv2's separate storage for SOP'. */
	if (p->svid_idx >= pd_get_svid_count(p->port, TCPC_TX_SOP)) {
		r->svid = 0;
		args->response_size = sizeof(r->svid);
		return EC_RES_SUCCESS;
	}

	r->svid = pd_get_svid(p->port, p->svid_idx, TCPC_TX_SOP);
	r->opos = 0;
	memcpy(r->vdo, pd_get_mode_vdo(p->port, p->svid_idx, TCPC_TX_SOP),
		sizeof(uint32_t) * PDO_MODES);
	modep = pd_get_amode_data(p->port, TCPC_TX_SOP, r->svid);

	if (modep)
		r->opos = pd_alt_mode(p->port, TCPC_TX_SOP, r->svid);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_GET_AMODE,
		     hc_remote_pd_get_amode,
		     EC_VER_MASK(0));

#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

#ifdef CONFIG_COMMON_RUNTIME
static enum ec_status hc_remote_pd_dev_info(struct host_cmd_handler_args *args)
{
	const uint8_t *port = args->params;
	struct ec_params_usb_pd_rw_hash_entry *r = args->response;
	uint16_t dev_id;
	uint32_t current_image;

	if (*port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	pd_dev_get_rw_hash(*port, &dev_id, r->dev_rw_hash, &current_image);

	r->dev_id = dev_id;
	r->current_image = current_image;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_DEV_INFO,
		     hc_remote_pd_dev_info,
		     EC_VER_MASK(0));

static const enum pd_dual_role_states dual_role_map[USB_PD_CTRL_ROLE_COUNT] = {
	[USB_PD_CTRL_ROLE_TOGGLE_ON]    = PD_DRP_TOGGLE_ON,
	[USB_PD_CTRL_ROLE_TOGGLE_OFF]   = PD_DRP_TOGGLE_OFF,
	[USB_PD_CTRL_ROLE_FORCE_SINK]   = PD_DRP_FORCE_SINK,
	[USB_PD_CTRL_ROLE_FORCE_SOURCE] = PD_DRP_FORCE_SOURCE,
	[USB_PD_CTRL_ROLE_FREEZE]       = PD_DRP_FREEZE,
};

static const mux_state_t typec_mux_map[USB_PD_CTRL_MUX_COUNT] = {
	[USB_PD_CTRL_MUX_NONE] = USB_PD_MUX_NONE,
	[USB_PD_CTRL_MUX_USB]  = USB_PD_MUX_USB_ENABLED,
	[USB_PD_CTRL_MUX_AUTO] = USB_PD_MUX_DP_ENABLED,
	[USB_PD_CTRL_MUX_DP]   = USB_PD_MUX_DP_ENABLED,
	[USB_PD_CTRL_MUX_DOCK] = USB_PD_MUX_DOCK,
};

/*
 * Combines the following information into a single byte
 * Bit 0: Active/Passive cable
 * Bit 1: Optical/Non-optical cable
 * Bit 2: Legacy Thunderbolt adapter
 * Bit 3: Active Link Uni-Direction/Bi-Direction
 */
static uint8_t get_pd_control_flags(int port)
{
	union tbt_mode_resp_cable cable_resp;
	union tbt_mode_resp_device device_resp;

	if (!IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP))
		return 0;

	cable_resp.raw_value = pd_get_tbt_mode_vdo(port, TCPC_TX_SOP_PRIME);
	device_resp.raw_value = pd_get_tbt_mode_vdo(port, TCPC_TX_SOP);

	/*
	 * Ref: USB Type-C Cable and Connector Specification
	 * Table F-11 TBT3 Cable Discover Mode VDO Responses
	 * For Passive cables, Active Cable Plug link training is set to 0
	 */
	return (cable_resp.lsrx_comm == UNIDIR_LSRX_COMM ?
			USB_PD_CTRL_ACTIVE_LINK_UNIDIR : 0) |
		(device_resp.tbt_adapter == TBT_ADAPTER_TBT2_LEGACY ?
			USB_PD_CTRL_TBT_LEGACY_ADAPTER : 0) |
		(cable_resp.tbt_cable == TBT_CABLE_OPTICAL ?
			USB_PD_CTRL_OPTICAL_CABLE : 0) |
		(cable_resp.retimer_type == USB_RETIMER ?
			USB_PD_CTRL_ACTIVE_CABLE : 0);
}

static uint8_t pd_get_role_flags(int port)
{
	return (pd_get_power_role(port) == PD_ROLE_SOURCE ?
			PD_CTRL_RESP_ROLE_POWER : 0) |
		(pd_get_data_role(port) == PD_ROLE_DFP ?
			PD_CTRL_RESP_ROLE_DATA : 0) |
		(pd_get_vconn_state(port) ?
			PD_CTRL_RESP_ROLE_VCONN : 0) |
		(pd_get_partner_dual_role_power(port) ?
			PD_CTRL_RESP_ROLE_DR_POWER : 0) |
		(pd_get_partner_data_swap_capable(port) ?
			PD_CTRL_RESP_ROLE_DR_DATA : 0) |
		(pd_get_partner_usb_comm_capable(port) ?
			PD_CTRL_RESP_ROLE_USB_COMM : 0) |
		(pd_get_partner_unconstr_power(port) ?
			PD_CTRL_RESP_ROLE_UNCONSTRAINED : 0);
}

static enum ec_status hc_usb_pd_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_control *p = args->params;
	struct ec_response_usb_pd_control_v2 *r_v2 = args->response;
	struct ec_response_usb_pd_control_v1 *r_v1 = args->response;
	struct ec_response_usb_pd_control *r = args->response;
	const char *task_state_name;
	mux_state_t mux_state;

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
			    pd_get_polarity(p->port));

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
		r_v2->enabled =
			(pd_comm_is_enabled(p->port) ?
				PD_CTRL_RESP_ENABLED_COMMS : 0) |
			(pd_is_connected(p->port) ?
				PD_CTRL_RESP_ENABLED_CONNECTED : 0) |
			(pd_capable(p->port) ?
				PD_CTRL_RESP_ENABLED_PD_CAPABLE : 0);
		r_v2->role = pd_get_role_flags(p->port);
		r_v2->polarity = pd_get_polarity(p->port);

		r_v2->cc_state =  pd_get_task_cc_state(p->port);
		task_state_name = pd_get_task_state_name(p->port);
		if (task_state_name)
			strzcpy(r_v2->state, task_state_name,
				sizeof(r_v2->state));
		else
			r_v2->state[0] = '\0';

		r_v2->control_flags = get_pd_control_flags(p->port);
		if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
			r_v2->dp_mode = get_dp_pin_mode(p->port);
			mux_state = usb_mux_get(p->port);
			if (mux_state & USB_PD_MUX_USB4_ENABLED) {
				r_v2->cable_speed =
					get_usb4_cable_speed(p->port);
			} else if (mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED) {
				r_v2->cable_speed =
					get_tbt_cable_speed(p->port);
				r_v2->cable_gen =
					get_tbt_rounded_support(p->port);
			}
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
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_CONTROL,
		     hc_usb_pd_control,
		     EC_VER_MASK(0) | EC_VER_MASK(1) | EC_VER_MASK(2));
#endif /* CONFIG_COMMON_RUNTIME */

#if defined(CONFIG_HOSTCMD_FLASHPD) && defined(CONFIG_USB_PD_TCPMV2)
static enum ec_status hc_remote_flash(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_fw_update *p = args->params;
	int port = p->port;
	int rv = EC_RES_SUCCESS;
	const uint32_t *data = &(p->size) + 1;
	int i, size;

	if (port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (p->size + sizeof(*p) > args->params_size)
		return EC_RES_INVALID_PARAM;

#if defined(CONFIG_CHARGE_MANAGER) && defined(CONFIG_BATTERY) && \
	(defined(CONFIG_BATTERY_PRESENT_CUSTOM) ||   \
	 defined(CONFIG_BATTERY_PRESENT_GPIO))
	/*
	 * Do not allow PD firmware update if no battery and this port
	 * is sinking power, because we will lose power.
	 */
	if (battery_is_present() != BP_YES &&
			charge_manager_get_active_charge_port() == port)
		return EC_RES_UNAVAILABLE;
#endif

	switch (p->cmd) {
	case USB_PD_FW_REBOOT:
		pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_REBOOT, NULL, 0);
		/*
		 * Return immediately to free pending i2c bus.  Host needs to
		 * manage this delay.
		 */
		return EC_RES_SUCCESS;

	case USB_PD_FW_FLASH_ERASE:
		pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_FLASH_ERASE, NULL, 0);
		/*
		 * Return immediately.  Host needs to manage delays here which
		 * can be as long as 1.2 seconds on 64KB RW flash.
		 */
		return EC_RES_SUCCESS;

	case USB_PD_FW_ERASE_SIG:
		pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_ERASE_SIG, NULL, 0);
		break;

	case USB_PD_FW_FLASH_WRITE:
		/* Data size must be a multiple of 4 */
		if (!p->size || p->size % 4)
			return EC_RES_INVALID_PARAM;

		size = p->size / 4;
		for (i = 0; i < size; i += VDO_MAX_SIZE - 1) {
			pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_FLASH_WRITE,
				data + i, MIN(size - i, VDO_MAX_SIZE - 1));
		}
		return EC_RES_SUCCESS;

	default:
		return EC_RES_INVALID_PARAM;
	}

	return rv;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_FW_UPDATE,
			hc_remote_flash,
			EC_VER_MASK(0));
#endif /* CONFIG_HOSTCMD_FLASHPD && CONFIG_USB_PD_TCPMV2 */

#ifdef CONFIG_HOSTCMD_EVENTS
void pd_notify_dp_alt_mode_entry(void)
{
	/*
	 * Note: EC_HOST_EVENT_PD_MCU may be a more appropriate host event to
	 * send, but we do not send that here because there are other cases
	 * where we send EC_HOST_EVENT_PD_MCU such as charger insertion or
	 * removal.  Currently, those do not wake the system up, but
	 * EC_HOST_EVENT_MODE_CHANGE does.  If we made the system wake up on
	 * EC_HOST_EVENT_PD_MCU, we would be turning the internal display on on
	 * every charger insertion/removal, which is not desired.
	 */
	CPRINTS("Notifying AP of DP Alt Mode Entry...");
	host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
}
#endif /* CONFIG_HOSTCMD_EVENTS */

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

	if (IS_ENABLED(CONFIG_USB_VPD) ||
	    IS_ENABLED(CONFIG_USB_CTVPD))
		r->pd_data_role_cap = EC_PD_DATA_ROLE_UFP;
	else
		r->pd_data_role_cap = EC_PD_DATA_ROLE_DUAL;

	/* Allow boards to override the locations from UNKNOWN if desired */
	r->pd_port_location = board_get_pd_port_location(p->port);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PD_PORT_CAPS,
		     hc_get_pd_port_caps,
		     EC_VER_MASK(0));

#ifdef CONFIG_HOSTCMD_PD_CONTROL
static enum ec_status pd_control(struct host_cmd_handler_args *args)
{
	static int pd_control_disabled[CONFIG_USB_PD_PORT_MAX_COUNT];
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
		/*
		 * The AP is requesting to suspend PD traffic on the EC so it
		 * can perform a firmware upgrade. If Vbus is present on the
		 * connector (it is either a source or sink), then we will
		 * prevent the upgrade if there is not enough battery to finish
		 * the upgrade. We cannot rely on the EC's active charger data
		 * as the EC just rebooted into RW and has not necessarily
		 * picked the active charger yet.
		 */
#ifdef HAS_TASK_CHARGER
		if (pd_is_vbus_present(cmd->chip)) {
			struct batt_params batt = { 0 };
			/*
			 * The charger task has not re-initialized, so we need
			 * to ask the battery directly.
			 */
			battery_get_params(&batt);
			if (batt.remaining_capacity <
				    MIN_BATTERY_FOR_TCPC_UPGRADE_MAH ||
			    batt.flags & BATT_FLAG_BAD_REMAINING_CAPACITY) {
				CPRINTS("C%d: Cannot suspend for upgrade, not "
					"enough battery (%dmAh)!",
					cmd->chip, batt.remaining_capacity);
				return EC_RES_BUSY;
			}
		}
#else
		if (pd_is_vbus_present(cmd->chip)) {
			CPRINTS("C%d: Cannot suspend for upgrade, Vbus "
				"present!",
				cmd->chip);
			return EC_RES_BUSY;
		}
#endif
		enable = 0;
	} else if (cmd->subcmd == PD_RESUME) {
		enable = 1;
	} else if (cmd->subcmd == PD_RESET) {
#ifdef HAS_TASK_PDCMD
		board_reset_pd_mcu();
#else
		return EC_RES_INVALID_COMMAND;
#endif
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

#if !defined(CONFIG_USB_PD_TCPM_STUB) && !defined(TEST_BUILD)
/*
 * PD host event status for host command
 * Note: this variable must be aligned on 4-byte boundary because we pass the
 * address to deprecated_atomic_ functions which use assembly to access them.
 */
static uint32_t pd_host_event_status __aligned(4);

static enum ec_status
hc_pd_host_event_status(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_status *r = args->response;

	/* Read and clear the host event status to return to AP */
	r->status = deprecated_atomic_read_clear(&pd_host_event_status);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_HOST_EVENT_STATUS, hc_pd_host_event_status,
		     EC_VER_MASK(0));

/* Send host event up to AP */
void pd_send_host_event(int mask)
{
	/* mask must be set */
	if (!mask)
		return;

	deprecated_atomic_or(&pd_host_event_status, mask);
	/* interrupt the AP */
	host_set_single_event(EC_HOST_EVENT_PD_MCU);
}
#endif /* ! CONFIG_USB_PD_TCPM_STUB && ! TEST_BUILD */

#endif /* HAS_TASK_HOSTCMD */
