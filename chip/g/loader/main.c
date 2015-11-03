/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "debug_printf.h"
#include "printf.h"
#include "system.h"
#include "uart.h"

/*
 * This file is a proof of concept stub which will be extended and split into
 * appropriate pieces sortly, when full blown support for cr50 bootrom is
 * introduced.
 */
uint32_t sleep_mask;

timestamp_t get_time(void)
{
	timestamp_t ret;

	ret.val = 0;

	return ret;
}

static int panic_txchar(void *context, int c)
{
	if (c == '\n')
		panic_txchar(context, '\r');

	/* Wait for space in transmit FIFO */
	while (!uart_tx_ready())
		;

	/* Write the character directly to the transmit FIFO */
	uart_write_char(c);

	return 0;
}

void panic_puts(const char *outstr)
{
	/* Put all characters in the output buffer */
	while (*outstr)
		panic_txchar(NULL, *outstr++);
}

void panic_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfnprintf(panic_txchar, NULL, format, args);
	va_end(args);
}

int main(void)
{
	debug_printf("Hello world\n");
	while (1)
		;
}

void panic_reboot(void)
{
	panic_puts("\n\nRebooting...\n");
	system_reset(0);
}

void interrupt_disable(void)
{
	asm("cpsid i");
}

static int printchar(void *context, int c)
{
	if (c == '\n')
		uart_write_char('\r');
	uart_write_char(c);

	return 0;
}

void debug_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfnprintf(printchar, NULL, format, args);
	va_end(args);
}
