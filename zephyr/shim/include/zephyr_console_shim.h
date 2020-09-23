/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_CONSOLE_SHIM_H
#define __CROS_EC_ZEPHYR_CONSOLE_SHIM_H

#include <shell/shell.h>

/**
 * zshim_run_ec_console_command() - Dispatch a CrOS EC console command
 * using Zephyr's shell
 *
 * @handler:		A CrOS EC shell command handler.
 * @shell:		The Zephyr shell to run on.
 * @argc:		The number of command line arguments.
 * @argv:		The NULL-terminated list of arguments.
 * @help_str:		The help string to display when "-h" is passed.
 * @argdesc:		The string describing the arguments to the command.
 *
 * Return: the return value from the handler.
 */
int zshim_run_ec_console_command(int (*handler)(int argc, char **argv),
				 const struct shell *shell, size_t argc,
				 char **argv, const char *help_str,
				 const char *argdesc);

/* Internal wrappers for DECLARE_CONSOLE_COMMAND_* macros. */
#define _ZEPHYR_SHELL_COMMAND_SHIM_2(NAME, ROUTINE_ID, ARGDESC, HELP,	\
				     WRAPPER_ID)			\
	static int WRAPPER_ID(const struct shell *shell, size_t argc,        \
			      char **argv)                                   \
	{                                                                    \
		return zshim_run_ec_console_command(ROUTINE_ID, shell, argc, \
						    argv, HELP, ARGDESC);    \
	}                                                                    \
	SHELL_CMD_ARG_REGISTER(NAME, NULL, HELP, WRAPPER_ID, 0,              \
			       SHELL_OPT_ARG_MAX)

#define _ZEPHYR_SHELL_COMMAND_SHIM(NAME, ROUTINE_ID, ARGDESC, HELP)   \
	_ZEPHYR_SHELL_COMMAND_SHIM_2(NAME, ROUTINE_ID, ARGDESC, HELP, \
				     UTIL_CAT(zshim_wrapper_, ROUTINE_ID))

/* These macros mirror the macros provided by the CrOS EC. */
#define DECLARE_CONSOLE_COMMAND(NAME, ROUTINE, ARGDESC, HELP) \
	_ZEPHYR_SHELL_COMMAND_SHIM(NAME, ROUTINE, ARGDESC, HELP)

/*
 * TODO(jrosenth): implement flags and restricted commands?  We just
 * discard this in the shim layer for now.
 */
#define DECLARE_CONSOLE_COMMAND_FLAGS(NAME, ROUTINE, ARGDESC, HELP, FLAGS) \
	_ZEPHYR_SHELL_COMMAND_SHIM(NAME, ROUTINE, ARGDESC, HELP)
#define DECLARE_SAFE_CONSOLE_COMMAND(NAME, ROUTINE, ARGDESC, HELP) \
	_ZEPHYR_SHELL_COMMAND_SHIM(NAME, ROUTINE, ARGDESC, HELP)

#endif  /* __CROS_EC_ZEPHYR_CONSOLE_SHIM_H */
