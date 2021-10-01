/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "base_state.h"
#include "console.h"
#include "host_command.h"
#include "hooks.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_LID, format, ## args)

#ifdef CONFIG_BASE_ATTACHED_SWITCH
/* 1: base attached, 0: otherwise */
static int base_state;

int base_get_state(void)
{
	return base_state;
}

void base_set_state(int state)
{
	if (base_state == !!state)
		return;

	base_state = !!state;
	CPRINTS("base state: %stached", state ? "at" : "de");
	hook_notify(HOOK_BASE_ATTACHED_CHANGE);

	/* Notify host of mode change. This likely will wake it up. */
	host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
}
#endif

static int command_setbasestate(int argc, char **argv)
{
	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;
	if (argv[1][0] == 'a')
		base_force_state(EC_SET_BASE_STATE_ATTACH);
	else if (argv[1][0] == 'd')
		base_force_state(EC_SET_BASE_STATE_DETACH);
	else if (argv[1][0] == 'r')
		base_force_state(EC_SET_BASE_STATE_RESET);
	else
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(basestate, command_setbasestate,
	"[attach | detach | reset]",
	"Manually force base state to attached, detached or reset.");

static enum ec_status hostcmd_setbasestate(struct host_cmd_handler_args *args)
{
	const struct ec_params_set_base_state *params = args->params;

	if (params->cmd > EC_SET_BASE_STATE_RESET)
		return EC_RES_INVALID_PARAM;

	base_force_state(params->cmd);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SET_BASE_STATE, hostcmd_setbasestate,
		     EC_VER_MASK(0));
