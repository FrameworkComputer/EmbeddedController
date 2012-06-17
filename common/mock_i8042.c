/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock EC i8042 interface code.
 */

#include "i8042.h"
#include "timer.h"
#include "uart.h"


void i8042_receives_data(int data)
{
	/* Not implemented */
	return;
}


void i8042_receives_command(int cmd)
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
	uart_printf("i8042 SEND\n");
	return EC_SUCCESS;
}


void i8042_enable_keyboard_irq(void) {
	/* Not implemented */
	return;
}


void i8042_disable_keyboard_irq(void) {
	/* Not implemented */
	return;
}


void i8042_flush_buffer()
{
	/* Not implemented */
	return;
}
