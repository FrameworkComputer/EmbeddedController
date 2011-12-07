/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* console.h - Debug console for Chrome EC */

#ifndef __CROS_EC_CONSOLE_H
#define __CROS_EC_CONSOLE_H

#include "common.h"

/* Console command */
struct console_command {
	/* Command name.  Case-insensitive. */
	const char *name;
	/* Handler for the command.  argv[0] will be the command name. */
	int (*handler)(int argc, char **argv);
};


/* Console command group */
struct console_group {
	const char *group_name;  /* Name of the command group */
	const struct console_command *commands;  /* List of commands */
	int command_count;  /* Number of commands in list */
};


/* Initializes the console module. */
int console_init(void);


/* Called by UART when a line of input is pending. */
void console_has_input(void);


/* Registers a group of console commands. */
int console_register_commands(const struct console_group *group);

#endif  /* __CROS_EC_CONSOLE_H */
