/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <shell/shell.h>
#include <string.h>
#include <sys/printk.h>
#include <zephyr.h>

#include "console.h"

int cputs(enum console_channel channel, const char *str)
{
	return cprintf(channel, "%s\n", str);
}

static const struct shell *current_shell;

static void console_vprintf(enum console_channel channel, const char *format,
			    va_list args)
{
	if (current_shell && channel == CC_COMMAND) {
		shell_vfprintf(current_shell, SHELL_NORMAL, format, args);
		return;
	}

	/*
	 * TODO(jrosenth): investigate using the logging subsystem
	 * and generating modules for the channels instead of printk
	 */
	vprintk(format, args);
}

__attribute__((__format__(__printf__, 2, 3))) int
cprintf(enum console_channel channel, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	console_vprintf(channel, format, args);
	va_end(args);
	return 0;
}

__attribute__((__format__(__printf__, 2, 3))) int
cprints(enum console_channel channel, const char *format, ...)
{
	va_list args;

	cprintf(channel, "[%lld ", k_uptime_get());
	va_start(args, format);
	console_vprintf(channel, format, args);
	va_end(args);
	cprintf(channel, "]\n");
	return 0;
}

void cflush(void)
{
	/*
	 * Do nothing.  Output is sent immediately without buffering
	 * from a printk() in Zephyr.
	 */
}

int zshim_run_ec_console_command(int (*handler)(int argc, char **argv),
				 const struct shell *shell, size_t argc,
				 char **argv, const char *help_str,
				 const char *argdesc)
{
	for (int i = 1; i < argc; i++) {
		if (!help_str && !argdesc)
			break;
		if (!strcmp(argv[i], "-h")) {
			if (help_str)
				shell_fprintf(shell, SHELL_NORMAL, "%s\n",
					      help_str);
			if (argdesc)
				shell_fprintf(shell, SHELL_NORMAL,
					      "Usage: %s\n", argdesc);
			return 0;
		}
	}

	current_shell = shell;
	return handler(argc, argv);
}
