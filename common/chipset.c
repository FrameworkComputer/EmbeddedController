/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chipset common code for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_POWER_AP
static int command_apreset(int argc, char **argv)
{
	int is_cold = 1;

	if (argc > 1 && !strcasecmp(argv[1], "cold"))
		is_cold = 1;
	else if (argc > 1 && !strcasecmp(argv[1], "warm"))
		is_cold = 0;

	/* Force the chipset to reset */
	ccprintf("Issuing AP %s reset...\n", is_cold ? "cold" : "warm");
	chipset_reset(is_cold);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apreset, command_apreset,
			"[warm | cold]",
			"Issue AP reset",
			NULL);

static int command_apshutdown(int argc, char **argv)
{
	chipset_force_shutdown();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apshutdown, command_apshutdown,
			NULL,
			"Force AP shutdown",
			NULL);
#endif
