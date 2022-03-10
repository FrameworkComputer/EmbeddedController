/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <shell/shell.h>
#include <stdlib.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

/* Console commands */
static int powerinfo_handler(const struct shell *shell, size_t argc,
							char **argv)
{
	int state;

	state = pwr_sm_get_state();
	shell_fprintf(shell, SHELL_INFO, "Power state = %d (%s)\n",
					state, pwrsm_dbg[state]);
	return 0;
}

SHELL_CMD_REGISTER(powerinfo, NULL, NULL, powerinfo_handler);

static int powerindebug_handler(const struct shell *shell, size_t argc,
							char **argv)
{
	int i;
	char *e;
	power_signal_mask_t current;

	/* If one arg, set the mask */
	if (argc == 2) {
		int m = strtol(argv[1], &e, 0);

		if (*e)
			return -EINVAL;

		power_set_debug(m);
	}

	/* Print the mask */
	current = power_get_signals();
	shell_fprintf(shell, SHELL_INFO, "power in:   0x%04x\n", current);
	shell_fprintf(shell, SHELL_INFO, "debug mask: 0x%04x\n",
						power_get_debug());

	/* Print the decode */
	shell_fprintf(shell, SHELL_INFO, "bit meanings:\n");
	for (i = 0; i < POWER_SIGNAL_COUNT; i++) {
		power_signal_mask_t mask = POWER_SIGNAL_MASK(i);

		shell_fprintf(shell, SHELL_INFO, "  0x%04x %d %s\n",
			mask, (current & mask) ? 1 : 0,
			power_signal_name(i));
	}

	return 0;
};

SHELL_CMD_REGISTER(powerindebug, NULL,
	"[mask] Get/set power input debug mask", powerindebug_handler);


static int apshutdown_handler(const struct shell *shell, size_t argc,
							char **argv)
{
	apshutdown();
	return 0;
}

SHELL_CMD_REGISTER(apshutdown, NULL, NULL, apshutdown_handler);

static int apreset_handler(const struct shell *shell, size_t argc,
							char **argv)
{
	ap_power_reset(AP_POWER_SHUTDOWN_CONSOLE_CMD);
	return 0;
}

SHELL_CMD_REGISTER(apreset, NULL, NULL, apreset_handler);

/* End of console commands */
