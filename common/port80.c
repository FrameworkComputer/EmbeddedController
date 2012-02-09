/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Port 80 module for Chrome EC */

#include "board.h"
#include "console.h"
#include "port80.h"
#include "uart.h"
#include "util.h"


#define HISTORY_LEN 16

static uint8_t history[HISTORY_LEN];
static int head = 0;  /* Next index to use / oldest previous entry */
static int last_data = -1;  /* Last data written to port 80 */


void port_80_write(int data)
{
#ifndef CONFIG_PORT80_PRINT_DUPLICATES
	/* Ignore duplicate writes, since the linux kernel writes to port 80
	 * as a delay mechanism during boot. */
	if (data == last_data)
		return;
#endif

	/* TODO: post to SWI and print from there?  This currently
	 * prints from inside the LPC interrupt itself. */

	uart_printf("[Port 80: 0x%02x]\n", data);
	last_data = data;

	history[head] = data;
	head = (head + 1) & (HISTORY_LEN - 1);
}


/*****************************************************************************/
/* Console commands */

static int command_port80(int argc, char **argv)
{
	int h = head;
	int i;

	/* Technically, if a port 80 write comes in while we're
	 * printing this, we could print an incorrect history.
	 * Probably not worth the complexity to work around that. */

	uart_puts("Last port 80 writes:");
	for (i = 0; i < HISTORY_LEN; i++)
		uart_printf(" %02x", history[(h + i) & (HISTORY_LEN - 1)]);
	uart_puts(" <--newest\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(port80, command_port80);

/*****************************************************************************/
/* Initialization */

int port_80_init(void)
{
	memset(history, 0, sizeof(history));
	return EC_SUCCESS;
}
