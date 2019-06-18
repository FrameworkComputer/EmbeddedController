/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "uartn.h"
#include "util.h"

static uint8_t done_uart_init_yet;

#define USE_UART_INTERRUPTS (!(defined(CONFIG_CUSTOMIZED_RO) && \
			       defined(SECTION_IS_RO)))
#ifndef UARTN
#define UARTN 0
#endif

int uart_init_done(void)
{
	return done_uart_init_yet;
}

void uart_tx_start(void)
{
	uartn_tx_start(UARTN);
}

void uart_tx_stop(void)
{
	uartn_tx_stop(UARTN);
}

int uart_tx_in_progress(void)
{
	return uartn_tx_in_progress(UARTN);
}

void uart_tx_flush(void)
{
	uartn_tx_flush(UARTN);
}

int uart_tx_ready(void)
{
	/* True if the TX buffer is not completely full */
	return uartn_tx_ready(UARTN);
}

int uart_rx_available(void)
{
	/* True if the RX buffer is not completely empty. */
	return uartn_rx_available(UARTN);
}

void uart_write_char(char c)
{
	uartn_write_char(UARTN, c);
}

int uart_read_char(void)
{
	return uartn_read_char(UARTN);
}

#if USE_UART_INTERRUPTS

/**
 * Interrupt handlers for UART0
 */
static void uart_console_tx_interrupt(void)
{
	/* Clear transmit interrupt status */
	GR_UART_ISTATECLR(UARTN) = GC_UART_ISTATECLR_TX_MASK;

	/* Fill output FIFO */
	uart_process_output();
}
DECLARE_IRQ(GC_IRQNUM_UART0_TXINT, uart_console_tx_interrupt, 1);

static void uart_console_rx_interrupt(void)
{
	/* Clear receive interrupt status */
	GR_UART_ISTATECLR(UARTN) = GC_UART_ISTATECLR_RX_MASK;

	/* Read input FIFO until empty */
	uart_process_input();
}
DECLARE_IRQ(GC_IRQNUM_UART0_RXINT, uart_console_rx_interrupt, 1);
#endif  /* USE_UART_INTERRUPTS */

void uart_init(void)
{
	clock_enable_module(MODULE_UART, 1);

	/* Initialize the Cr50 UART */
	uartn_init(UARTN);
	uartn_enable(UARTN);

#ifdef UART_AP
	uartn_init(UART_AP);
#endif
#ifdef UART_EC
	uartn_init(UART_EC);
#endif

	done_uart_init_yet = 1;
}
