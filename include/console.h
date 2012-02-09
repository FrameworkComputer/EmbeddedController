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


/* Initializes the console module. */
int console_init(void);


/* Called by UART when a line of input is pending. */
void console_has_input(void);

/* Register a console command handler */
#define DECLARE_CONSOLE_COMMAND(name, routine)			\
	static const char __con_cmd_label_##name[] = #name;	\
	const struct console_command __con_cmd_##name		\
		__attribute__((section(".rodata.cmds")))	\
		= {__con_cmd_label_##name, routine}

#endif  /* __CROS_EC_CONSOLE_H */
