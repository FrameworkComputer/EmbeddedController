/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
}

void uart_flush_output(void)
{
}

void uart_tx_flush(void)
{
}
