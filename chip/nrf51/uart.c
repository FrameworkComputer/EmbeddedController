/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USART driver for Chrome EC */

#include "clock.h"
#include "console.h"
#include "common.h"
#include "dma.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)


static int ever_sent;    /* if we ever sent byte to TXD? */
static int init_done;    /* Initialization done? */
static int should_stop;  /* Last TX control action */

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	disable_sleep(SLEEP_MASK_UART);
	should_stop = 0;
	NRF51_UART_INTENSET = BIT(NRF55_UART_TXDRDY_BIT);
	task_trigger_irq(NRF51_PERID_USART);
}

void uart_tx_stop(void)
{
	NRF51_UART_INTENCLR = BIT(NRF55_UART_TXDRDY_BIT);
	should_stop = 1;
	enable_sleep(SLEEP_MASK_UART);
}

int uart_tx_ready(void)
{
	/*
	 * nRF51 design is NOT tx-empty style. Instead, it is if a byte is
	 * ever transmitted from TxD. This means NRF51_UART_TXDRDY is always
	 * 0 after reset. So, we use 'ever_sent' to send the first byte.
	 */
	return NRF51_UART_TXDRDY || (!ever_sent);
}

int uart_rx_available(void)
{
	return NRF51_UART_RXDRDY;
}

void uart_tx_flush(void)
{
	while (!uart_tx_ready())
		;
}

void uart_write_char(char c)
{
	ever_sent = 1;
	NRF51_UART_TXDRDY = 0;
	NRF51_UART_TXD = c;
	NRF51_UART_STARTTX = 1;
}

int uart_read_char(void)
{
	NRF51_UART_RXDRDY = 0;
	return NRF51_UART_RXD;
}

/* Interrupt handler for console USART */
void uart_interrupt(void)
{
#ifndef CONFIG_UART_RX_DMA
	/*
	 * Read input FIFO until empty.  DMA-based receive does this from a
	 * hook in the UART buffering module.
	 */
	uart_process_input();
#endif

	/* Fill output FIFO */
	uart_process_output();

#ifndef CONFIG_UART_TX_DMA
	if (!should_stop)
		NRF51_UART_INTENSET = BIT(NRF55_UART_TXDRDY_BIT);
#endif  /* CONFIG_UART_TX_DMA */

}
DECLARE_IRQ(NRF51_PERID_USART, uart_interrupt, 2);


void uart_init(void)
{
	NRF51_UART_PSELTXD = NRF51_UART_TX_PIN;   /* GPIO Port for Tx */
	NRF51_UART_PSELRXD = NRF51_UART_RX_PIN;   /* GPIO Port for Rx */
	NRF51_UART_CONFIG = 0;    /* disable HW flow control, no parity bit */
	NRF51_UART_BAUDRATE = 0x01d7e000;  /* 115200 */
	NRF51_UART_ENABLE = 0x4;  /* Enable UART */

	task_enable_irq(NRF51_PERID_USART);

	NRF51_UART_INTENSET = BIT(NRF55_UART_RXDRDY_BIT);
	NRF51_UART_STARTRX = 1;

	init_done = 1;
}
