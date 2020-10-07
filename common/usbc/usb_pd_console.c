/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "usb_common.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"
#include "usb_pd.h"
#include "util.h"

test_export_static int command_pd(int argc, char **argv)
{
	int port;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "dump")) {
		if (argc >= 3) {
			int level = strtoi(argv[2], &e, 10);

			if (*e)
				return EC_ERROR_PARAM2;

			if (level < DEBUG_DISABLE)
				level = DEBUG_DISABLE;
			else if (level > DEBUG_LEVEL_MAX)
				level = DEBUG_LEVEL_MAX;

			prl_set_debug_level(level);
			pe_set_debug_level(level);
			tc_set_debug_level(level);
			ccprintf("debug=%d\n", level);
			return EC_SUCCESS;
		}
	} else if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC) &&
				!strcasecmp(argv[1], "trysrc")) {
		enum try_src_override_t ov = tc_get_try_src_override();

		if (argc >= 3) {
			ov = strtoi(argv[2], &e, 10);
			if (*e || ov > TRY_SRC_NO_OVERRIDE)
				return EC_ERROR_PARAM3;
			tc_try_src_override(ov);
		}

		if (ov == TRY_SRC_NO_OVERRIDE)
			ccprintf("Try.SRC System controlled\n");
		else
			ccprintf("Try.SRC Forced %s\n", ov ? "ON" : "OFF");

		return EC_SUCCESS;
	} else if (!strcasecmp(argv[1], "version")) {
		ccprintf("%d\n", PD_STACK_VERSION);
		return EC_SUCCESS;
	}

	/* command: pd <port> <subcmd> [args] */
	port = strtoi(argv[1], &e, 10);
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	if (*e || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return EC_ERROR_PARAM2;

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE)) {
		if (!strcasecmp(argv[2], "tx")) {
			pd_dpm_request(port, DPM_REQUEST_SNK_STARTUP);
		} else if (!strcasecmp(argv[2], "charger")) {
			pd_dpm_request(port, DPM_REQUEST_SRC_STARTUP);
		} else if (!strcasecmp(argv[2], "dev")) {
			int max_volt;

			if (argc >= 4) {
				max_volt = strtoi(argv[3], &e, 10) * 1000;
				if (*e)
					return EC_ERROR_PARAM3;
			} else {
				max_volt = pd_get_max_voltage();
			}
			pd_request_source_voltage(port, max_volt);
			pd_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);
			ccprintf("max req: %dmV\n", max_volt);
		} else if (!strcasecmp(argv[2], "disable")) {
			pd_comm_enable(port, 0);
			ccprintf("Port C%d disable\n", port);
			return EC_SUCCESS;
		} else if (!strcasecmp(argv[2], "enable")) {
			pd_comm_enable(port, 1);
			ccprintf("Port C%d enabled\n", port);
			return EC_SUCCESS;
		} else if (!strcasecmp(argv[2], "hard")) {
			pd_dpm_request(port, DPM_REQUEST_HARD_RESET_SEND);
		} else if (!strcasecmp(argv[2], "soft")) {
			pd_dpm_request(port, DPM_REQUEST_SOFT_RESET_SEND);
		} else if (!strcasecmp(argv[2], "swap")) {
			if (argc < 4)
				return EC_ERROR_PARAM_COUNT;

			if (!strcasecmp(argv[3], "power"))
				pd_dpm_request(port, DPM_REQUEST_PR_SWAP);
			else if (!strcasecmp(argv[3], "data"))
				pd_dpm_request(port, DPM_REQUEST_DR_SWAP);
			else if (IS_ENABLED(CONFIG_USBC_VCONN_SWAP) &&
					!strcasecmp(argv[3], "vconn"))
				pd_dpm_request(port, DPM_REQUEST_VCONN_SWAP);
			else
				return EC_ERROR_PARAM3;
		} else if (!strcasecmp(argv[2], "dualrole")) {
			if (argc < 4) {
				ccprintf("dual-role toggling: ");
				switch (pd_get_dual_role(port)) {
				case PD_DRP_TOGGLE_ON:
					ccprintf("on\n");
					break;
				case PD_DRP_TOGGLE_OFF:
					ccprintf("off\n");
					break;
				case PD_DRP_FREEZE:
					ccprintf("freeze\n");
					break;
				case PD_DRP_FORCE_SINK:
					ccprintf("force sink\n");
					break;
				case PD_DRP_FORCE_SOURCE:
					ccprintf("force source\n");
					break;
				}
			} else {
				if (!strcasecmp(argv[3], "on"))
					pd_set_dual_role(port,
							PD_DRP_TOGGLE_ON);
				else if (!strcasecmp(argv[3], "off"))
					pd_set_dual_role(port,
							PD_DRP_TOGGLE_OFF);
				else if (!strcasecmp(argv[3], "freeze"))
					pd_set_dual_role(port, PD_DRP_FREEZE);
				else if (!strcasecmp(argv[3], "sink"))
					pd_set_dual_role(port,
							PD_DRP_FORCE_SINK);
				else if (!strcasecmp(argv[3], "source"))
					pd_set_dual_role(port,
							PD_DRP_FORCE_SOURCE);
				else
					return EC_ERROR_PARAM4;
			}
			return EC_SUCCESS;
		}
	}

	if (!strcasecmp(argv[2], "state")) {
		ccprintf("Port C%d CC%d, %s - Role: %s-%s",
		port, pd_get_polarity(port) + 1,
		pd_comm_is_enabled(port) ? "Enable" : "Disable",
		pd_get_power_role(port) ==
					PD_ROLE_SOURCE ? "SRC" : "SNK",
		pd_get_data_role(port) == PD_ROLE_DFP ? "DFP" : "UFP");

		if (IS_ENABLED(CONFIG_USBC_VCONN))
			ccprintf("%s ", tc_is_vconn_src(port) ? "-VC" : "");

		ccprintf("TC State: %s, Flags: 0x%04x",
			tc_get_current_state(port),
			tc_get_flags(port));

		if (IS_ENABLED(CONFIG_USB_PE_SM))
			ccprintf(" PE State: %s, Flags: 0x%04x\n",
				pe_get_current_state(port),
				pe_get_flags(port));
		else
			ccprintf("\n");
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pd, command_pd,
	 "version"
	 "\ndump [0|1|2|3]"
#ifdef CONFIG_USB_PD_TRY_SRC
	"\ntrysrc [0|1|2]"
#endif
	"\n\t<port> state"
#ifdef CONFIG_USB_PD_DUAL_ROLE
	"|tx|charger|dev"
	"\n\t<port> disable|enable|soft|hard"
	"\n\t<port> dualrole [on|off|freeze|sink|source]"
	"\n\t<port> swap [power|data|vconn]"
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	,
	"USB PD");
