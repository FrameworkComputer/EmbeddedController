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
#include "printf.h"

int cputs(enum console_channel channel, const char *str)
{
	return cprintf(channel, "%s", str);
}

static int printk_putchar(void *context, int c)
{
	printk("%c", c);
	return 0;
}

static void console_vprintf(enum console_channel channel, const char *format,
			    va_list args)
{
	/*
	 * TODO(jrosenth): investigate using the logging subsystem
	 * and generating modules for the channels instead of printk
	 *
	 * TODO(b/170658516): If logging doesn't work, then we should at least
	 * use shell_ print functions instead of printk function as they could
	 * be on different uarts (they are not for Chrome OS Apps though).
	 */
	vfnprintf(printk_putchar, NULL, format, args);
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

	cprintf(channel, "[%pT ", PRINTF_TIMESTAMP_NOW);
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
	ARG_UNUSED(shell);

	for (int i = 1; i < argc; i++) {
		if (!help_str && !argdesc)
			break;
		if (!strcmp(argv[i], "-h")) {
			if (help_str)
				printk("%s\n", help_str);
			if (argdesc)
				printk("Usage: %s\n", argdesc);
			return 0;
		}
	}

	return handler(argc, argv);
}
