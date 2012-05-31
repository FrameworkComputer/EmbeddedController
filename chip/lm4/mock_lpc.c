/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock LPC module for Chrome EC */

#include "board.h"
#include "lpc.h"
#include "registers.h"
#include "uart.h"

void lpc_set_host_events(uint32_t mask)
{
	uart_printf("Host event: %x\n", mask);
}


uint32_t lpc_get_host_events(void)
{
	/* Not implemented */
	return 0;
}


void lpc_clear_host_events(uint32_t mask)
{
	uart_printf("Clear host event: %x\n", mask);
}


void lpc_set_host_event_mask(enum lpc_host_event_type type, uint32_t mask)
{
	uart_printf("Set host event mask: type %d = %x\n", type, mask);
}


uint32_t lpc_get_host_event_mask(enum lpc_host_event_type type)
{
	/* Not implemented */
	return 0;
}


int lpc_comx_has_char(void)
{
	/* Not implemented */
	return 0;
}


int lpc_comx_get_char(void)
{
	/* Not implemented */
	return 0;
}


void lpc_comx_put_char(int c)
{
	/* Not implemented */
	return;
}
