/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock LPC module for Chrome EC */

#include "common.h"
#include "ec_commands.h"
#include "lpc.h"
#include "registers.h"
#include "uart.h"


void lpc_set_host_event_state(uint32_t mask)
{
	uart_printf("Host event: %x\n", mask);
}


uint32_t lpc_get_host_event_state(void)
{
	/* Not implemented */
	return 0;
}


void lpc_clear_host_event_state(uint32_t mask)
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

#define LPC_POOL_OFFS_CMD_DATA 512  /* Data range for host commands - 512-767 */
#define LPC_POOL_CMD_DATA (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_CMD_DATA)

uint8_t *lpc_get_memmap_range(void)
{
	return (uint8_t *)LPC_POOL_CMD_DATA + EC_HOST_PARAM_SIZE * 2;
}


uint8_t *host_get_buffer(void)
{
	return (uint8_t *)LPC_POOL_CMD_DATA;
}
