/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock EC i8042 interface code.
 */

#include "i8042.h"
#include "timer.h"
#include "uart.h"

void i8042_receive(int data, int is_cmd)
{
	/* Not implemented */
	return;
}

void i8042_command_task(void)
{
	/* Do nothing */
	while (1)
		usleep(5000000);
}

enum ec_error_list i8042_send_to_host(int len, const uint8_t *bytes)
{
	int i;
	uart_printf("i8042 SEND:");
	for (i = 0; i < len; ++i)
		uart_printf(" %02x", bytes[i]);
	uart_printf("\n");
	return EC_SUCCESS;
}

void i8042_enable_keyboard_irq(int enable)
{
	/* Not implemented */
	return;
}

void i8042_flush_buffer()
{
	/* Not implemented */
	return;
}
