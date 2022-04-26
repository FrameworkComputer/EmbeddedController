/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_non_dsx_common_pwrseq_sm_handler.h>

/* Host commands */
static enum ec_status
host_command_reboot_ap_on_g3(struct host_cmd_handler_args *args)
{
	const struct ec_params_reboot_ap_on_g3_v1 *cmd = args->params;

	/* Store request for processing at g3 */
	request_exit_hardoff(true);

	switch (args->version) {
	case 0:
		break;
	case 1:
		/* Store user specified delay to wait in G3 state */
		set_reboot_ap_at_g3_delay_seconds(cmd->reboot_ap_at_g3_delay);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_REBOOT_AP_ON_G3,
		     host_command_reboot_ap_on_g3,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

/* End of host commands */
