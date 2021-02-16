/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <init.h>
#include <kernel.h>
#include <shell/shell.h>
#include <stdbool.h>
#include <string.h>
#include <sys/printk.h>
#include <zephyr.h>

#include "console.h"
#include "printf.h"
#include "uart.h"

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

#if DT_NODE_EXISTS(DT_PATH(ec_console))
#define EC_CONSOLE DT_PATH(ec_console)

static const char * const disabled_channels[] = DT_PROP(EC_CONSOLE, disabled);
static const size_t disabled_channel_count = DT_PROP_LEN(EC_CONSOLE, disabled);
static int init_ec_console(const struct device *unused)
{
	for (size_t i = 0; i < disabled_channel_count; i++)
		console_channel_disable(disabled_channels[i]);

	return 0;
} SYS_INIT(init_ec_console, PRE_KERNEL_1, 50);
#endif

/*
 * Minimal implementation of a few uart_* functions we need.
 * TODO(b/178033156): probably need to swap this for something more
 * robust in order to handle UART buffering.
 */

int uart_init_done(void)
{
	return true;
}

void uart_tx_start(void)
{
}

int uart_tx_ready(void)
{
	return 1;
}

int uart_tx_char_raw(void *context, int c)
{
	uart_write_char(c);
	return 0;
}

void uart_write_char(char c)
{
	printk("%c", c);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE))
		console_buf_notify_char(c);
}

void uart_flush_output(void)
{
}

void uart_tx_flush(void)
{
}
