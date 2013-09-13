/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "lpc.h"
#include "registers.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#define CONFIG_UART_HOST_IRQ CONCAT2(LM4_IRQ_UART, CONFIG_UART_HOST)

static int init_done;

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	/* If interrupt is already enabled, nothing to do */
	if (LM4_UART_IM(0) & 0x20)
		return;

	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.  This works around a hardware problem with the
	 * UART where the FIFO only triggers the interrupt when its
	 * threshold is _crossed_, not just met.
	 */
	LM4_UART_IM(0) |= 0x20;
	task_trigger_irq(LM4_IRQ_UART0);
}

void uart_tx_stop(void)
{
	LM4_UART_IM(0) &= ~0x20;
}

void uart_tx_flush(void)
{
	/* Wait for transmit FIFO empty */
	while (!(LM4_UART_FR(0) & 0x80))
		;
}

int uart_tx_ready(void)
{
	return !(LM4_UART_FR(0) & 0x20);
}

int uart_rx_available(void)
{
	return !(LM4_UART_FR(0) & 0x10);
}

void uart_write_char(char c)
{
	/* Wait for space in transmit FIFO. */
	while (!uart_tx_ready())
		;

	LM4_UART_DR(0) = c;
}

int uart_read_char(void)
{
	return LM4_UART_DR(0);
}

static void uart_clear_rx_fifo(int channel)
{
	int scratch __attribute__ ((unused));
	while (!(LM4_UART_FR(channel) & 0x10))
		scratch = LM4_UART_DR(channel);
}

void uart_disable_interrupt(void)
{
	task_disable_irq(LM4_IRQ_UART0);
}

void uart_enable_interrupt(void)
{
	task_enable_irq(LM4_IRQ_UART0);
}

/**
 * Interrupt handler for UART0
 */
static void uart_ec_interrupt(void)
{
	/* Clear transmit and receive interrupt status */
	LM4_UART_ICR(0) = 0x70;


	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();
}
DECLARE_IRQ(LM4_IRQ_UART0, uart_ec_interrupt, 1);

/**
 * Interrupt handler for Host UART
 */
static void uart_host_interrupt(void)
{
	/* Clear transmit and receive interrupt status */
	LM4_UART_ICR(CONFIG_UART_HOST) = 0x70;

#ifdef CONFIG_LPC
	/*
	 * If we have space in our FIFO and a character is pending in LPC,
	 * handle that character.
	 */
	if (!(LM4_UART_FR(CONFIG_UART_HOST) & 0x20) && lpc_comx_has_char()) {
		/* Copy the next byte then disable transmit interrupt */
		LM4_UART_DR(CONFIG_UART_HOST) = lpc_comx_get_char();
		LM4_UART_IM(CONFIG_UART_HOST) &= ~0x20;
	}

	/*
	 * Handle received character.  There is no flow control on input;
	 * received characters are blindly forwarded to LPC.  This is ok
	 * because LPC is much faster than UART, and we don't have flow control
	 * on the UART receive-side either.
	 */
	if (!(LM4_UART_FR(CONFIG_UART_HOST) & 0x10))
		lpc_comx_put_char(LM4_UART_DR(CONFIG_UART_HOST));
#endif
}
/* Must be same prio as LPC interrupt handler so they don't preempt */
DECLARE_IRQ(CONFIG_UART_HOST_IRQ, uart_host_interrupt, 2);

static void uart_config(int port)
{
	/* Disable the port */
	LM4_UART_CTL(port) = 0x0300;
	/* Use the internal oscillator */
	LM4_UART_CC(port) = 0x1;
	/* Set the baud rate divisor */
	LM4_UART_IBRD(port) = (INTERNAL_CLOCK / 16) / CONFIG_UART_BAUD_RATE;
	LM4_UART_FBRD(port) =
		(((INTERNAL_CLOCK / 16) % CONFIG_UART_BAUD_RATE) * 64
		 + CONFIG_UART_BAUD_RATE / 2) / CONFIG_UART_BAUD_RATE;
	/*
	 * 8-N-1, FIFO enabled.  Must be done after setting
	 * the divisor for the new divisor to take effect.
	 */
	LM4_UART_LCRH(port) = 0x70;
	/*
	 * Interrupt when RX fifo at minimum (>= 1/8 full), and TX fifo
	 * when <= 1/4 full
	 */
	LM4_UART_IFLS(port) = 0x01;
	/*
	 * Unmask receive-FIFO, receive-timeout.  We need
	 * receive-timeout because the minimum RX FIFO depth is 1/8 = 2
	 * bytes; without the receive-timeout we'd never be notified
	 * about single received characters.
	 */
	LM4_UART_IM(port) = 0x50;
	/* Enable the port */
	LM4_UART_CTL(port) |= 0x0001;
}

void uart_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable UART0 and Host UART and delay a few clocks */
	LM4_SYSTEM_RCGCUART |= (1 << CONFIG_UART_HOST) | 1;
	scratch = LM4_SYSTEM_RCGCUART;

	gpio_config_module(MODULE_UART, 1);

	/* Configure UARTs (identically) */
	uart_config(0);
	uart_config(CONFIG_UART_HOST);

	/*
	 * Enable interrupts for UART0 only. Host UART will have to wait
	 * until the LPC bus is initialized.
	 */
	uart_clear_rx_fifo(0);
	task_enable_irq(LM4_IRQ_UART0);

	init_done = 1;
}

/*****************************************************************************/
/* COMx functions */

void uart_comx_enable(void)
{
	uart_clear_rx_fifo(CONFIG_UART_HOST);
	task_enable_irq(CONFIG_UART_HOST_IRQ);
}

int uart_comx_putc_ok(void)
{
	if (LM4_UART_FR(CONFIG_UART_HOST) & 0x20) {
		/*
		 * FIFO is full, so enable transmit interrupt to let us know
		 * when it empties.
		 */
		LM4_UART_IM(CONFIG_UART_HOST) |= 0x20;
		return 0;
	} else {
		return 1;
	}
}

void uart_comx_putc(int c)
{
	LM4_UART_DR(CONFIG_UART_HOST) = c;
}

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_COMXTEST

/**
 * Write a character to COMx, waiting for space in the output buffer if
 * necessary.
 */
static void uart_comx_putc_wait(int c)
{
		while (!uart_comx_putc_ok())
			;
		uart_comx_putc(c);
}

static int command_comxtest(int argc, char **argv)
{
	/* Put characters to COMX port */
	const char *c = argc > 1 ? argv[1] : "testing comx output!";

	ccprintf("Writing \"%s\\r\\n\" to COMx UART...\n", c);

	while (*c)
		uart_comx_putc_wait(*c++);

	uart_comx_putc_wait('\r');
	uart_comx_putc_wait('\n');

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(comxtest, command_comxtest,
			"[string]",
			"Write test data to COMx uart",
			NULL);

#endif /* CONFIG_CMD_COMX_TEST */
