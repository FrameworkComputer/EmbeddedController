/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for MEC1322 */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "lpc.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

static int init_done;

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	/* If interrupt is already enabled, nothing to do */
	if (MEC1322_UART_IER & (1 << 1))
		return;

	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.  This works around a hardware problem with the
	 * UART where the FIFO only triggers the interrupt when its
	 * threshold is _crossed_, not just met.
	 */
	MEC1322_UART_IER |= (1 << 1);
	task_trigger_irq(MEC1322_IRQ_UART);
}

void uart_tx_stop(void)
{
	MEC1322_UART_IER &= ~(1 << 1);
}

void uart_tx_flush(void)
{
	/* Wait for transmit FIFO empty */
	while (!(MEC1322_UART_LSR & (1 << 5)))
		;
}

int uart_tx_ready(void)
{
	/*
	 * TODO(crosbug.com/p/24107): This is FIFO empty bit instead of
	 *                            FIFO full bit?
	 */
	return MEC1322_UART_LSR & (1 << 5);
}

int uart_rx_available(void)
{
	return MEC1322_UART_LSR & (1 << 0);
}

void uart_write_char(char c)
{
	/* Wait for space in transmit FIFO. */
	while (!uart_tx_ready())
		;

	MEC1322_UART_TB = c;
}

int uart_read_char(void)
{
	return MEC1322_UART_RB;
}

static void uart_clear_rx_fifo(int channel)
{
	MEC1322_UART_FCR = (1 << 0) | (1 << 1);
}

void uart_disable_interrupt(void)
{
	task_disable_irq(MEC1322_IRQ_UART);
}

void uart_enable_interrupt(void)
{
	task_enable_irq(MEC1322_IRQ_UART);
}

/**
 * Interrupt handler for UART
 */
void uart_ec_interrupt(void)
{
	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();
}
DECLARE_IRQ(MEC1322_IRQ_UART, uart_ec_interrupt, 1);

void uart_init(void)
{
	/* Set UART to reset on VCC1_RESET instaed of nSIO_RESET */
	MEC1322_UART_CFG &= ~(1 << 1);

	/* Baud rate = 115200. 1.8432MHz clock. Divisor = 1 */

	/* Set CLK_SRC = 0 */
	MEC1322_UART_CFG &= ~(1 << 0);

	/* Set DLAB = 1 */
	MEC1322_UART_LCR |= (1 << 7);

	/* PBRG0/PBRG1 */
	MEC1322_UART_PBRG0 = 1;
	MEC1322_UART_PBRG1 = 0;

	/* Set DLAB = 0 */
	MEC1322_UART_LCR &= ~(1 << 7);

	/* Set word length to 8-bit */
	MEC1322_UART_LCR |= (1 << 0) | (1 << 1);

	/* Enable FIFO */
	MEC1322_UART_FCR = (1 << 0);

	/* Activate UART */
	MEC1322_UART_ACT |= (1 << 0);

	/*
	clock_enable_peripheral(CGC_OFFSET_UART, mask,
			CGC_MODE_RUN | CGC_MODE_SLEEP);*/

	gpio_config_module(MODULE_UART, 1);

	/*
	 * Enable interrupts for UART0.
	 */
	uart_clear_rx_fifo(0);
	MEC1322_UART_IER |= (1 << 0);
	MEC1322_UART_MCR |= (1 << 3);
	MEC1322_INT_ENABLE(15) |= (1 << 0);
	MEC1322_INT_BLK_EN |= (1 << 15);
	task_enable_irq(MEC1322_IRQ_UART);

	init_done = 1;
}
