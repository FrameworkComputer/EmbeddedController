/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

static int done_uart_init_yet;

int uart_init_done(void)
{
	return done_uart_init_yet;
}

void uart_tx_start(void)
{
	/* If interrupt is already enabled, nothing to do */
	if (G_UART_ICTRL(0) & 0x01)
		return;

	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);

	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.  This works around a hardware problem with the
	 * UART where the FIFO only triggers the interrupt when its
	 * threshold is _crossed_, not just met.
	 */
	/* TODO(crosbug.com/p/33432): Do we need this hack here? Find out. */
	REG_WRITE_MASK(G_UART_ICTRL(0), 0x01, 0x01, 0);
	task_trigger_irq(G_IRQNUM_UART0_TXINT);
}

void uart_tx_stop(void)
{
	/* Disable the TX interrupt */
	REG_WRITE_MASK(G_UART_ICTRL(0), 0x01, 0x00, 0);

	/* Re-allow deep sleep */
	enable_sleep(SLEEP_MASK_UART);
}

int uart_tx_in_progress(void)
{
	/* Transmit is in progress if the TX idle bit is not set */
	return !(G_UART_STATE(0) & 0x20);
}

void uart_tx_flush(void)
{
	/* Wait until TX FIFO is idle. */
	while (uart_tx_in_progress())
		;
}

int uart_tx_ready(void)
{
	/* True if the TX buffer is not completely full */
	return !(G_UART_STATE(0) & 0x01);
}

int uart_rx_available(void)
{
	/* True if the RX buffer is not completely empty. */
	/* TODO(crosbug.com/p/33432): Want a single bit (b2/18273495) */
	return G_UART_RFIFO(0) & 0x0fc0;
}

void uart_write_char(char c)
{
	/* Wait for space in transmit FIFO. */
	while (!uart_tx_ready())
		;

	G_UART_WDATA(0) = c;
}

int uart_read_char(void)
{
	return G_UART_RDATA(0);
}

void uart_disable_interrupt(void)
{
	task_disable_irq(G_IRQNUM_UART0_TXINT);
	task_disable_irq(G_IRQNUM_UART0_RXINT);
}

void uart_enable_interrupt(void)
{
	task_enable_irq(G_IRQNUM_UART0_TXINT);
	task_enable_irq(G_IRQNUM_UART0_RXINT);
}

/**
 * Interrupt handlers for UART0
 */
void uart_ec_tx_interrupt(void)
{
	/* Clear transmit interrupt status */
	G_UART_ISTATECLR(0) = 0x01;

	/* Fill output FIFO */
	uart_process_output();
}
DECLARE_IRQ(G_IRQNUM_UART0_TXINT, uart_ec_tx_interrupt, 1);

void uart_ec_rx_interrupt(void)
{
	/* Clear receive interrupt status */
	G_UART_ISTATECLR(0) = 0x02;

	/* Read input FIFO until empty */
	uart_process_input();
}
DECLARE_IRQ(G_IRQNUM_UART0_RXINT, uart_ec_rx_interrupt, 1);

/* Constants for setting baud rate */
#define PCLK_FREQ  26000000
#define DEFAULT_UART_FREQ 1000000
#define UART_NCO_WIDTH 16

void uart_init(void)
{
	long long setting = (16 * (1 << UART_NCO_WIDTH) *
			     (long long)CONFIG_UART_BAUD_RATE / PCLK_FREQ);

	/* turn on uart clock */
	clock_enable_module(MODULE_UART, 1);

	/* set up pinmux */
	G_PINMUX_DIOA0_SEL = G_PINMUX_UART0_TX_SEL;
	G_PINMUX_UART0_RX_SEL = G_PINMUX_DIOA1_SEL;

	/* IE must be set to 1 to work as a digital pad (for any direction) */
	/* turn on input driver (IE field) */
	REG_WRITE_MASK(G_PINMUX_DIOA0_CTL, G_PINMUX_DIOA0_CTL_IE_MASK,
		       1, G_PINMUX_DIOA0_CTL_IE_LSB);
	/* turn on input driver (IE field) */
	REG_WRITE_MASK(G_PINMUX_DIOA1_CTL, G_PINMUX_DIOA1_CTL_IE_MASK,
		       1, G_PINMUX_DIOA1_CTL_IE_LSB);

	/* set frequency */
	G_UART_NCO(0) = setting;

	/* Interrupt when RX fifo has anything, when TX fifo <= half empty */
	/* Also reset (clear) both FIFOs */
	G_UART_FIFO(0) = 0x63;

	/* TX enable, RX enable, HW flow control disabled, no loopback */
	G_UART_CTRL(0) = 0x03;

	/* enable RX interrupts in block */
	/* Note: doesn't do anything unless turned on in NVIC */
	G_UART_ICTRL(0) = 0x02;

	/* Enable interrupts for UART0 only */
	uart_enable_interrupt();

	done_uart_init_yet = 1;
}
