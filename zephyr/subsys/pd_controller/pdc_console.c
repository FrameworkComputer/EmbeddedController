/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "uart.h"
#include "usb_common.h"

#include <stdint.h>
#include <stdlib.h>

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <drivers/pdc.h>
#include <usbc/pdc_power_mgmt.h>

static int cmd_get_pd_port(const struct shell *sh, char *arg_val, uint8_t *port)
{
	char *e;

	*port = strtoul(arg_val, &e, 0);
	if (*e || *port >= CONFIG_USB_PD_PORT_MAX_COUNT) {
		shell_error(sh, "Invalid port");
		return -EINVAL;
	}

	return 0;
}

static int cmd_pdc_get_status(const struct shell *sh, size_t argc, char **argv)
{
	int rv;
	uint8_t port;
	enum pd_power_role pr;
	enum pd_data_role dr;
	enum tcpc_cc_polarity polarity;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	/* Get PDC Status */
	pr = pdc_power_mgmt_get_power_role(port);
	dr = pdc_power_mgmt_pd_get_data_role(port);
	polarity = pdc_power_mgmt_pd_get_polarity(port);
	shell_fprintf(sh, SHELL_INFO,
		      "Port C%d CC%d, %s - Role: %s-%s PDC State: %s"
		      "\n",
		      port, polarity + 1,
		      pdc_power_mgmt_is_connected(port) ? "Enable" : "Disable",
		      pr == PD_ROLE_SINK ? "SNK" : "SRC",
		      dr == PD_ROLE_DFP ? "DFP" : "UFP",
		      pdc_power_mgmt_get_task_state_name(port));

	return EC_SUCCESS;
}

static int cmd_pdc_get_connector_status(const struct shell *sh, size_t argc,
					char **argv)
{
	int rv;
	uint8_t port;
	union connector_status_t connector_status;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	rv = pdc_power_mgmt_get_connector_status(port, &connector_status);
	if (rv)
		return rv;

	shell_fprintf(sh, SHELL_INFO, "Port %d GET_CONNECTOR_STATUS:\n", port);
	shell_fprintf(sh, SHELL_INFO,
		      "   change bits                      : 0x%04x\n",
		      connector_status.raw_conn_status_change_bits);
	shell_fprintf(sh, SHELL_INFO,
		      "   power_operation_mode             : %d\n",
		      connector_status.power_operation_mode);
	shell_fprintf(sh, SHELL_INFO,
		      "   connect_status                   : %d\n",
		      connector_status.connect_status);
	shell_fprintf(sh, SHELL_INFO,
		      "   power_direction                  : %d\n",
		      connector_status.power_direction);
	shell_fprintf(sh, SHELL_INFO,
		      "   conn_partner_flags               : 0x%02x\n",
		      connector_status.conn_partner_flags);
	shell_fprintf(sh, SHELL_INFO,
		      "   conn_partner_type                : %d\n",
		      connector_status.conn_partner_type);
	shell_fprintf(sh, SHELL_INFO,
		      "   rdo                              : 0x%08x\n",
		      connector_status.rdo);
	shell_fprintf(sh, SHELL_INFO,
		      "   battery_charging_cap_status      : %d\n",
		      connector_status.battery_charging_cap_status);
	shell_fprintf(sh, SHELL_INFO,
		      "   provider_caps_limited_reason     : %d\n",
		      connector_status.provider_caps_limited_reason);
	shell_fprintf(sh, SHELL_INFO,
		      "   bcd_pd_version                   : 0x%04x\n",
		      connector_status.bcd_pd_version);
	shell_fprintf(sh, SHELL_INFO,
		      "   orientation                      : %d\n",
		      connector_status.orientation);
	shell_fprintf(sh, SHELL_INFO,
		      "   sink_path_status                 : %d\n",
		      connector_status.sink_path_status);
	shell_fprintf(sh, SHELL_INFO,
		      "   reverse_current_protection_status: %d\n",
		      connector_status.reverse_current_protection_status);
	shell_fprintf(sh, SHELL_INFO,
		      "   power_reading_ready              : %d\n",
		      connector_status.power_reading_ready);
	shell_fprintf(sh, SHELL_INFO,
		      "   peak_current                     : %d\n",
		      connector_status.peak_current);
	shell_fprintf(sh, SHELL_INFO,
		      "   average_current                  : %d\n",
		      connector_status.average_current);
	shell_fprintf(sh, SHELL_INFO,
		      "   voltage_scale                    : %d\n",
		      connector_status.voltage_scale);
	shell_fprintf(sh, SHELL_INFO,
		      "   voltage_reading                  : %d\n",
		      connector_status.voltage_reading);
	shell_fprintf(sh, SHELL_INFO,
		      "   voltage                          : %d mV\n",
		      (connector_status.voltage_reading *
		       connector_status.voltage_scale * 5));

	return EC_SUCCESS;
}

static int cmd_pdc_get_cable_prop(const struct shell *sh, size_t argc,
				  char **argv)
{
	int rv;
	uint8_t port;
	union cable_property_t cable_prop;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	rv = pdc_power_mgmt_get_cable_prop(port, &cable_prop);
	if (rv)
		return rv;

	shell_fprintf(sh, SHELL_INFO, "Port %d GET_CABLE_PROP:\n", port);
	shell_fprintf(sh, SHELL_INFO,
		      "   bm_speed_supported               : 0x%04x\n",
		      cable_prop.bm_speed_supported);
	shell_fprintf(sh, SHELL_INFO,
		      "   b_current_capability             : %d mA\n",
		      cable_prop.b_current_capability * 50);
	shell_fprintf(sh, SHELL_INFO,
		      "   vbus_in_cable                    : %d\n",
		      cable_prop.vbus_in_cable);
	shell_fprintf(sh, SHELL_INFO,
		      "   cable_type                       : %d\n",
		      cable_prop.cable_type);
	shell_fprintf(sh, SHELL_INFO,
		      "   directionality                   : %d\n",
		      cable_prop.directionality);
	shell_fprintf(sh, SHELL_INFO,
		      "   plug_end_type                    : %d\n",
		      cable_prop.plug_end_type);
	shell_fprintf(sh, SHELL_INFO,
		      "   mode_support                     : %d\n",
		      cable_prop.mode_support);
	shell_fprintf(sh, SHELL_INFO,
		      "   cable_pd_revision                : %d\n",
		      cable_prop.cable_pd_revision);
	shell_fprintf(sh, SHELL_INFO,
		      "   latency                          : %d\n",
		      cable_prop.latency);

	return EC_SUCCESS;
}

static int cmd_pdc_get_info(const struct shell *sh, size_t argc, char **argv)
{
	int rv;
	uint8_t port;
	bool live = true;
	struct pdc_info_t pdc_info = { 0 };

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	if (argc > 2) {
		/* Parse optional live parameter */
		char *e;
		int live_param = strtoul(argv[2], &e, 0);
		if (*e) {
			shell_error(sh, "Pass 0/1 for live");
			return -EINVAL;
		}

		live = !!live_param;
	}

	/* Get PDC Status */
	rv = pdc_power_mgmt_get_info(port, &pdc_info, live);
	if (rv) {
		shell_error(sh, "Could not get port %u info (%d)", port, rv);
		return rv;
	}

	/* Check if the FW project name is set. */
	bool has_proj_name = pdc_info.project_name[0] != '\0' &&
			     pdc_info.project_name[0] != 0xFF;

	shell_fprintf(sh, SHELL_INFO,
		      "Live: %d\n"
		      "FW Ver: %u.%u.%u\n"
		      "PD Rev: %u\n"
		      "PD Ver: %u\n"
		      "VID/PID: %04x:%04x\n"
		      "Running Flash Code: %c\n"
		      "Flash Bank: %u\n"
		      "Project Name: '%s'\n",
		      live, PDC_FWVER_GET_MAJOR(pdc_info.fw_version),
		      PDC_FWVER_GET_MINOR(pdc_info.fw_version),
		      PDC_FWVER_GET_PATCH(pdc_info.fw_version),
		      pdc_info.pd_revision, pdc_info.pd_version,
		      PDC_VIDPID_GET_VID(pdc_info.vid_pid),
		      PDC_VIDPID_GET_PID(pdc_info.vid_pid),
		      pdc_info.is_running_flash_code ? 'Y' : 'N',
		      pdc_info.running_in_flash_bank,
		      has_proj_name ? pdc_info.project_name : "<None>");

	return EC_SUCCESS;
}

static int cmd_lpm_ppm_info(const struct shell *sh, size_t argc, char **argv)
{
	struct lpm_ppm_info_t info;
	uint8_t port;
	int rv;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	/* Get PDC info using UCSI GET_LPM_PPM_INFO command */
	rv = pdc_power_mgmt_get_lpm_ppm_info(port, &info);
	if (rv) {
		shell_error(sh, "Could not get port %u info (%d)", port, rv);
		return rv;
	}

	shell_fprintf(sh, SHELL_INFO,
		      "VID/PID: %04x:%04x\n"
		      "XID: %08x\n"
		      "FW Ver: %u.%u\n"
		      "HW Ver: %08x\n",
		      info.vid, info.pid, info.xid, info.fw_ver,
		      info.fw_ver_sub, info.hw_ver);

	return 0;
}

static int cmd_pdc_prs(const struct shell *sh, size_t argc, char **argv)
{
	int rv;
	uint8_t port;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	/* Trigger power role swap request */
	pdc_power_mgmt_request_power_swap(port);

	return EC_SUCCESS;
}

static int cmd_pdc_drs(const struct shell *sh, size_t argc, char **argv)
{
	int rv;
	uint8_t port;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	/* Verify port partner supports data role swaps */
	if (!pdc_power_mgmt_get_partner_data_swap_capable(port)) {
		shell_error(sh, "Port partner doesn't support drs");
		return -EIO;
	}

	/* Trigger data role swap request */
	pdc_power_mgmt_request_data_swap(port);

	return EC_SUCCESS;
}

static int cmd_pdc_dualrole(const struct shell *sh, size_t argc, char **argv)
{
	int rv;
	uint8_t port;
	enum pd_dual_role_states state;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	if (argc >= 3) {
		/* Set dual role state */
		if (!strcmp(argv[2], "on")) {
			state = PD_DRP_TOGGLE_ON;
		} else if (!strcmp(argv[2], "off")) {
			state = PD_DRP_TOGGLE_OFF;
		} else if (!strcmp(argv[2], "freeze")) {
			state = PD_DRP_FREEZE;
		} else if (!strcmp(argv[2], "sink")) {
			state = PD_DRP_FORCE_SINK;
		} else if (!strcmp(argv[2], "source")) {
			state = PD_DRP_FORCE_SOURCE;
		} else {
			shell_error(sh, "Invalid dualrole mode");
			return -EINVAL;
		}

		pdc_power_mgmt_set_dual_role(port, state);
	}

	/* Print current state */
	const char *state_str;

	state = pdc_power_mgmt_get_dual_role(port);

	switch (state) {
	case PD_DRP_TOGGLE_ON:
		state_str = "TOGGLE_ON";
		break;
	case PD_DRP_TOGGLE_OFF:
		state_str = "TOGGLE_OFF";
		break;
	case PD_DRP_FREEZE:
		state_str = "FREEZE";
		break;
	case PD_DRP_FORCE_SINK:
		state_str = "FORCE_SINK";
		break;
	case PD_DRP_FORCE_SOURCE:
		state_str = "FORCE_SOURCE";
		break;
	default:
		state_str = "Unknown";
		break;
	}

	shell_info(sh, "Dual role state: %s", state_str);

	return EC_SUCCESS;
}

static int cmd_pdc_trysrc(const struct shell *sh, size_t argc, char **argv)
{
	int rv;
	uint8_t enable = 0;
	char *e;

	enable = strtoul(argv[1], &e, 10);
	if (*e) {
		shell_error(sh, "unable to parse TrySrc value");
		return -EINVAL;
	}
	if (!(enable == 0 || enable == 1)) {
		shell_error(sh, "expecting [0|1]");
		return -EINVAL;
	}

	rv = pdc_power_mgmt_set_trysrc(0, enable);
	if (rv) {
		shell_error(sh, "Could not set trysrc %d", rv);
		return rv;
	}
	shell_info(sh, "Try.SRC Forced %s", enable ? "ON" : "OFF");
	return EC_SUCCESS;
}

static int cmd_pdc_reset(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t port;
	int rv;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	/* Trigger a PDC reset for this port. */
	rv = pdc_power_mgmt_reset(port);
	if (rv) {
		shell_error(sh, "Could not reset port %u (%d)", port, rv);
		return rv;
	}

	return EC_SUCCESS;
}

static int cmd_pdc_connector_reset(const struct shell *sh, size_t argc,
				   char **argv)
{
	int rv;
	uint8_t port;
	enum connector_reset reset_type;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	if (!strcmp(argv[2], "hard")) {
		reset_type = PD_HARD_RESET;
	} else if (!strcmp(argv[2], "data")) {
		reset_type = PD_DATA_RESET;
	} else {
		shell_error(sh, "Invalid connector reset type");
		return -EINVAL;
	}

	/* Trigger a PDC connector reset */
	rv = pdc_power_mgmt_connector_reset(port, reset_type);
	if (rv) {
		shell_error(sh, "CONNECTOR_RESET not sent to port %u (%d)",
			    port, rv);
	}

	return rv;
}

/* LCOV_EXCL_START - No known way to test tab-completion feature */
/**
 * @brief Tab-completion of "suspend" or "resume" for the comms subcommand
 */
static void pdc_console_get_suspend_or_resume(size_t idx,
					      struct shell_static_entry *entry)
{
	entry->syntax = NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;

	switch (idx) {
	case 0:
		entry->syntax = "suspend";
		return;
	case 1:
		entry->syntax = "resume";
		return;
	}
}

SHELL_DYNAMIC_CMD_CREATE(dsub_suspend_or_resume,
			 pdc_console_get_suspend_or_resume);
/* LCOV_EXCL_STOP */

static int cmd_pdc_comms_state(const struct shell *sh, size_t argc, char **argv)
{
	bool enable;
	int rv;

	/* Suspend or resume PDC comms */
	if (!strncmp(argv[1], "suspend", strlen("suspend"))) {
		shell_fprintf(sh, SHELL_INFO, "Suspend port threads\n");
		enable = false;
	} else if (!strncmp(argv[1], "resume", strlen("resume"))) {
		shell_fprintf(sh, SHELL_INFO, "Resume port threads\n");
		enable = true;
	} else {
		shell_error(sh, "Invalid value");
		return -EINVAL;
	}

	/* Apply to all ports
	 *
	 * TODO(b/323371550): This command should take a chip argument and
	 * target only ports serviced by that chip.
	 */
	rv = pdc_power_mgmt_set_comms_state(enable);

	if (rv) {
		shell_fprintf(sh, SHELL_ERROR, "Could not %s PDC: (%d)\n",
			      argv[1], rv);
	}

	return rv;
}

static int cmd_pdc_src_voltage(const struct shell *sh, size_t argc, char **argv)
{
	int rv;
	int mv;
	uint8_t port;
	char *e;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	if (argc > 2) {
		/* Request a particular voltage and convert to mV */
		mv = strtol(argv[2], &e, 10) * 1000;
		if (*e)
			return EC_ERROR_PARAM2;
	} else {
		/* Use max */
		mv = pd_get_max_voltage();
		shell_fprintf(sh, SHELL_INFO, "Using max voltage (%dmV)\n", mv);
	}

	shell_fprintf(sh, SHELL_INFO, "Requesting to source %dmV\n", mv);
	pd_request_source_voltage(port, mv);

	return 0;
}

static int cmd_pdc_srccaps(const struct shell *sh, size_t argc, char **argv)
{
	int rv;
	uint8_t port;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	const uint32_t *const src_caps = pdc_power_mgmt_get_src_caps(port);
	uint8_t src_caps_count = pdc_power_mgmt_get_src_cap_cnt(port);

	if (src_caps == NULL || src_caps_count == 0) {
		shell_fprintf(sh, SHELL_ERROR, "No source caps for port %u\n",
			      port);
		return 0;
	}

	for (uint8_t i = 0; i < src_caps_count; i++) {
		uint32_t src_cap = src_caps[i];
		uint32_t max_ma = 0, max_mv = 0, min_mv = 0;
		const char *type_str;

		pd_extract_pdo_power(src_cap, &max_ma, &max_mv, &min_mv);

		switch (src_cap & PDO_TYPE_MASK) {
		case PDO_TYPE_FIXED:
			type_str = "FIX";
			/* Fixed PDOs have flags and a single voltage */
			shell_fprintf(
				sh, SHELL_INFO,
				"Src %02u: %08x %s %13umV, %5umA "
				"[%s %s %s %s %s]\n",
				i, src_cap, type_str, max_mv, max_ma,
				src_cap & PDO_FIXED_DUAL_ROLE ? "DRP" : "   ",
				src_cap & PDO_FIXED_UNCONSTRAINED ? "UP" : "  ",
				src_cap & PDO_FIXED_COMM_CAP ? "USB" : "   ",
				src_cap & PDO_FIXED_DATA_SWAP ? "DRD" : "   ",
				src_cap & PDO_FIXED_FRS_CURR_MASK ? "FRS" :
								    "   ");
			continue;
		case PDO_TYPE_BATTERY:
			type_str = "BAT";
			break;
		case PDO_TYPE_VARIABLE:
			type_str = "VAR";
			break;
		case PDO_TYPE_AUGMENTED:
			type_str = "AUG";
			break;
		}

		/* Battery, variable, and augmented PDOs have voltage
		 * ranges but no flags.
		 */
		shell_fprintf(sh, SHELL_INFO,
			      "Src %02u: %08x %s %5umV-%5umV, %5um%c\n", i,
			      src_cap, type_str, min_mv, max_mv, max_ma,
			      (((src_cap & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) ?
				       'W' :
				       'A'));
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_pdc_cmds,
	SHELL_CMD_ARG(status, NULL,
		      "Get PD status\n"
		      "Usage: pdc status <port>",
		      cmd_pdc_get_status, 2, 0),
	SHELL_CMD_ARG(info, NULL,
		      "Get PDC chip info. Live defaults to 1 to force a new "
		      "read from chip. Pass 0 to use cached info.\n"
		      "Usage: pdc info <port> [live]",
		      cmd_pdc_get_info, 2, 1),
	SHELL_CMD_ARG(prs, NULL,
		      "Trigger power role swap\n"
		      "Usage: pdc prs <port>",
		      cmd_pdc_prs, 2, 0),
	SHELL_CMD_ARG(drs, NULL,
		      "Trigger data role swap\n"
		      "Usage: pdc drs <port>",
		      cmd_pdc_drs, 2, 0),
	SHELL_CMD_ARG(reset, NULL,
		      "Trigger a PDC reset\n"
		      "Usage: pdc reset <port>",
		      cmd_pdc_reset, 2, 0),
	SHELL_CMD_ARG(dualrole, NULL,
		      "Set or get dualrole mode\n"
		      "Usage: pdc dualrole  <port> [on|off|freeze|sink|source]",
		      cmd_pdc_dualrole, 2, 1),
	SHELL_CMD_ARG(trysrc, NULL,
		      "Set trysrc mode\n"
		      "Usage: pdc trysrc [0|1]",
		      cmd_pdc_trysrc, 2, 0),
	SHELL_CMD_ARG(conn_reset, NULL,
		      "Trigger hard or data reset\n"
		      "Usage: pdc conn_reset  <port> [hard|data]",
		      cmd_pdc_connector_reset, 3, 0),
	SHELL_CMD_ARG(comms, &dsub_suspend_or_resume,
		      "Suspend/resume PDC command communication\n"
		      "Usage: pdc comms [suspend|resume]",
		      cmd_pdc_comms_state, 2, 0),
	SHELL_CMD_ARG(connector_status, NULL,
		      "Print the UCSI GET_CONNECTOR_STATUS\n"
		      "Usage pdc connector_status <port>",
		      cmd_pdc_get_connector_status, 2, 0),
	SHELL_CMD_ARG(cable_prop, NULL,
		      "Print the UCSI GET_CABLE_PROPERTY\n"
		      "Usage pdc cable_prop <port>",
		      cmd_pdc_get_cable_prop, 2, 0),
	SHELL_CMD_ARG(src_voltage, NULL,
		      "Request to source a given voltage from PSU. "
		      "Omit last arg to use maximum supported voltage.\n"
		      "Usage: pdc src_voltage <port> [volts]",
		      cmd_pdc_src_voltage, 2, 1),
	SHELL_CMD_ARG(srccaps, NULL,
		      "Print current source capability PDOs received by the "
		      "given port.\n"
		      "Usage pdc srccaps <port>",
		      cmd_pdc_srccaps, 2, 0),
	SHELL_CMD_ARG(lpm_ppm_info, NULL,
		      "Get PDC chip info via GET_LPM_PPM_INFO UCSI cmd\n"
		      "Usage: pdc lpm_ppm_info <port>",
		      cmd_lpm_ppm_info, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(pdc, &sub_pdc_cmds, "PDC console commands", NULL);

static int cmd_pd_version(const struct shell *sh, size_t argc, char **argv)
{
	shell_fprintf(sh, SHELL_INFO, "3\n");
	return EC_SUCCESS;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pd_cmds,
			       SHELL_CMD(version, NULL,
					 "Get PD version\n"
					 "Usage: pd version",
					 cmd_pd_version),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(pd, &sub_pd_cmds, "PD commands (deprecated)", NULL);
