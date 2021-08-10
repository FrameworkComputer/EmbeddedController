/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_CONSOLE_SHIM_H
#define __CROS_EC_ZEPHYR_CONSOLE_SHIM_H

#include <shell/shell.h>

struct zephyr_console_command {
	/* Handler for the command.  argv[0] will be the command name. */
	int (*handler)(int argc, char **argv);
	/* Description of args */
	const char *argdesc;
	/* Short help for command */
	const char *help;
};

/**
 * zshim_run_ec_console_command() - Dispatch a CrOS EC console command
 * using Zephyr's shell
 *
 * @command:		Pointer to a struct zephyr_console_command
 * @argc:		The number of command line arguments.
 * @argv:		The NULL-terminated list of arguments.
 *
 * Return: the return value from the handler.
 */
int zshim_run_ec_console_command(const struct zephyr_console_command *command,
				 size_t argc, char **argv);

/* Internal wrappers for DECLARE_CONSOLE_COMMAND_* macros. */
#define _ZEPHYR_SHELL_COMMAND_SHIM_2(NAME, ROUTINE_ID, ARGDESC, HELP,       \
				     WRAPPER_ID, ENTRY_ID)                  \
	static const struct zephyr_console_command ENTRY_ID = {             \
		.handler = ROUTINE_ID,                                      \
		.argdesc = ARGDESC,                                         \
		.help = HELP,                                               \
	};                                                                  \
	static int WRAPPER_ID(const struct shell *shell, size_t argc,       \
			      char **argv)                                  \
	{                                                                   \
		return zshim_run_ec_console_command(&ENTRY_ID, argc, argv); \
	}                                                                   \
	SHELL_CMD_ARG_REGISTER(NAME, NULL, HELP, WRAPPER_ID, 0,             \
			       SHELL_OPT_ARG_MAX)

#define _ZEPHYR_SHELL_COMMAND_SHIM(NAME, ROUTINE_ID, ARGDESC, HELP)        \
	_ZEPHYR_SHELL_COMMAND_SHIM_2(NAME, ROUTINE_ID, ARGDESC, HELP,      \
				     UTIL_CAT(zshim_wrapper_, ROUTINE_ID), \
				     UTIL_CAT(zshim_entry_, ROUTINE_ID))

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

/**
 * console_buf_notify_chars() - Notify the console host command buffer
 * of bytes on the console.
 *
 * @s:			The pointer to the string.
 * @len:		The size of the string.
 */
void console_buf_notify_chars(const char *s, size_t len);

#endif /* __CROS_EC_ZEPHYR_CONSOLE_SHIM_H */
