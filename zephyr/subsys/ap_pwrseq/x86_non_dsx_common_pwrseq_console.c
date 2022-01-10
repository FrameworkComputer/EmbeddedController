/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <shell/shell.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

LOG_MODULE_DECLARE(ap_pwrseq, 4);

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
	chipset_reset(PWRSEQ_CHIPSET_SHUTDOWN_CONSOLE_CMD);
	return 0;
}

SHELL_CMD_REGISTER(apreset, NULL, NULL, apreset_handler);

/* End of console commands */
