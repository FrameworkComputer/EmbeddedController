/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "uartn.h"

void uartn_dma_init(uint8_t uart_num)
{
	/* Disable the Power Down of the MDMA module */
	NPCX_PWDWN_CTL(NPCX_PMC_PWDWN_9) &= ~BIT(uart_num);
}

void uartn_dma_rx_init(uint8_t uart_num)
{
	/* Set Receive FIFO Level to 1 */
	SET_FIELD(NPCX_UFRCTL(uart_num), NPCX_UFRCTL_RFULL_LVL_SEL, 0x01);
	/* Set UART receiving to use the MDMA mode */
	SET_BIT(NPCX_UMDSL(uart_num), NPCX_UMDSL_ERD);
}

void uartn_dma_start_rx(uint8_t uart_num, const void *memory, uint32_t count)
{
	NPCX_MDMA_DSTB0(uart_num) = (uint32_t)memory;
	NPCX_MDMA_TCNT0(uart_num) = count;

	SET_BIT(NPCX_MDMA_CTL0(uart_num), NPCX_MDMA_CTL_MDMAEN);
}
uint32_t uartn_dma_rx_bytes_done(uint8_t uart_num)
{
	return NPCX_MDMA_TCNT0(uart_num) - NPCX_MDMA_CTCNT0(uart_num);
}

void uartn_dma_reset(uint8_t uart_num)
{
	NPCX_SWRST_CTL(SWRST_CTL4) |= BIT(NPCX_SWRST_CTL4_MDMA_RST(uart_num));
	NPCX_SWRST_TRG = 0x0;
	NPCX_SWRST_TRG = NPCX_SWRST_TRG_WORD;
	while (NPCX_SWRST_TRG != 0xFFFF)
		;
}
