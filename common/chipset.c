/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset common code for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "system.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_POWER_AP
static int command_apreset(int argc, const char **argv)
{
	/* Force the chipset to reset */
	ccprintf("Issuing AP reset...\n");
	chipset_reset(CHIPSET_RESET_CONSOLE_CMD);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apreset, command_apreset, NULL, "Issue AP reset");

static int command_apshutdown(int argc, const char **argv)
{
	/*
	 * TODO: Fix the problem that CHIPSET_SHUTDOWN_CONSOLE_CMD is
	 * overwritten by CHIPSET_SHUTDOWN_POWERFAIL (in intel_x86.c).
	 */
	if (IS_ENABLED(CONFIG_POWER_BUTTON_INIT_IDLE)) {
		chip_save_reset_flags(chip_read_reset_flags() |
				      EC_RESET_FLAG_AP_IDLE);
		system_set_reset_flags(EC_RESET_FLAG_AP_IDLE);
		CPRINTS("Saved AP_IDLE flag");
	}

	chipset_force_shutdown(CHIPSET_SHUTDOWN_CONSOLE_CMD);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apshutdown, command_apshutdown, NULL,
			"Force AP shutdown");

#endif

#ifdef CONFIG_HOSTCMD_AP_RESET
static enum ec_status host_command_apreset(struct host_cmd_handler_args *args)
{
	/* Force the chipset to reset */
	chipset_reset(CHIPSET_RESET_HOST_CMD);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_AP_RESET, host_command_apreset, EC_VER_MASK(0));

#endif
