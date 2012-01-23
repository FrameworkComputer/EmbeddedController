/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#include "console.h"
#include "registers.h"
#include "uart.h"
#include "util.h"
#include "version.h"


static int command_write_word(int argc, char **argv)
{
	volatile uint32_t *address;
	uint32_t value;

	if (argc != 3) {
		uart_puts("Usage: ww <address> <value>\n");
		return EC_ERROR_UNKNOWN;
	}
	address = (uint32_t*)strtoi(argv[1], NULL, 0);
	value = strtoi(argv[2], NULL, 0);

	uart_printf("write word 0x%p = 0x%08x\n", address, value);
	uart_flush_output();

	*address = value;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ww, command_write_word);
DECLARE_CONSOLE_COMMAND(writeword, command_write_word);


static int command_read_word(int argc, char **argv)
{
	volatile uint32_t *address;
	uint32_t value;

	if (argc != 2) {
		uart_puts("Usage: rw <address>\n");
		return EC_ERROR_UNKNOWN;
	}
	address = (uint32_t*)strtoi(argv[1], NULL, 0);
	value = *address;

	uart_printf("read word 0x%p = 0x%08x\n", address, value);
	uart_flush_output();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rw, command_read_word);
DECLARE_CONSOLE_COMMAND(readword, command_read_word);
