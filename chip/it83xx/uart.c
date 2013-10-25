/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/* Traces on UART0 */
#define UART_PORT 0

static int init_done;

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	/* If interrupt is already enabled, nothing to do */
	if (IT83XX_UART_IER(UART_PORT) & 0x02)
		return;

	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);

	/* Re-enable the transmit interrupt. */
	IT83XX_UART_IER(UART_PORT) |= 0x02;
}

void uart_tx_stop(void)
{
	IT83XX_UART_IER(UART_PORT) &= ~0x02;

	/* Re-allow deep sleep */
	enable_sleep(SLEEP_MASK_UART);
}

void uart_tx_flush(void)
{
	/*
	 * Wait for transmit FIFO empty (TEMT) and transmitter holder
	 * register and transmitter shift registers to be empty (THRE).
	 */
	while ((IT83XX_UART_LSR(UART_PORT) & 0x60) != 0x60)
		;
}

int uart_tx_ready(void)
{
	/* Transmit is ready when FIFO is empty (THRE). */
	return IT83XX_UART_LSR(UART_PORT) & 0x20;
}

int uart_tx_in_progress(void)
{
	/*
	 * Transmit is in progress if transmit holding register or transmitter
	 * shift register are not empty (TEMT).
	 */
	return !(IT83XX_UART_LSR(UART_PORT) & 0x40);
}

int uart_rx_available(void)
{
	return IT83XX_UART_LSR(UART_PORT) & 0x01;
}

void uart_write_char(char c)
{
	/* Wait for space in transmit FIFO. */
	while (!uart_tx_ready())
		;

	IT83XX_UART_THR(UART_PORT) = c;
}

int uart_read_char(void)
{
	return IT83XX_UART_RBR(UART_PORT);
}

void uart_disable_interrupt(void)
{
	task_disable_irq(IT83XX_IRQ_UART1);
}

void uart_enable_interrupt(void)
{
	task_enable_irq(IT83XX_IRQ_UART1);
}

static void uart_ec_interrupt(void)
{
	/* clear interrupt status */
	task_clear_pending_irq(IT83XX_IRQ_UART1);

	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();
}
DECLARE_IRQ(IT83XX_IRQ_UART1, uart_ec_interrupt, 1);

static void uart_config(void)
{
#if PLL_CLOCK == 48000000
	/* Set CLK_UART_DIV_SEL to /2. Assumes PLL is 48 MHz. */
	IT83XX_ECPM_SCDCR1 |= 0x01;

	/*
	 * Specify clock source of the UART is 24MHz,
	 * must match CLK_UART_DIV_SEL.
	 */
	IT83XX_UART_CSSR(UART_PORT) = 0x01;
#else
#error "Support only for PLL clock speed of 48MHz."
#endif

	/* 8-N-1 and DLAB set to allow access to DLL and DLM registers. */
	IT83XX_UART_LCR(UART_PORT) = 0x83;

	/* Set divisor to set baud rate to 115200 */
	IT83XX_UART_DLM(UART_PORT) = 0x00;
	IT83XX_UART_DLL(UART_PORT) = 0x01;

	/*
	 * Clear DLAB bit to exclude access to DLL and DLM and give access to
	 * RBR and THR.
	 */
	IT83XX_UART_LCR(UART_PORT) = 0x03;

	/*
	 * Enable TX and RX FIFOs and set RX FIFO interrupt level to the
	 * minimum 1 byte.
	 */
	IT83XX_UART_FCR(UART_PORT) = 0x07;

	/*
	 * set OUT2 bit to enable interrupt logic.
	 */
	IT83XX_UART_MCR(UART_PORT) = 0x08;
}

void uart_init(void)
{
	/* Waiting for when we can use the GPIO module to set pin muxing */
	gpio_config_module(MODULE_UART, 1);

	/* switch UART0 on without hardware flow control */
	IT83XX_GPIO_GRC1 = 0x01;
	IT83XX_GPIO_GRC6 |= 0x03;

	/* Enable clocks to UART 1 and 2. */
	clock_enable_peripheral(CGC_OFFSET_UART, 0, 0);

	/* Config UART 0 only for now. */
	uart_config();

	/* clear interrupt status */
	task_clear_pending_irq(IT83XX_IRQ_UART1);

	/* Enable interrupts */
	IT83XX_UART_IER(UART_PORT) = 0x03;
	task_enable_irq(IT83XX_IRQ_UART1);

	init_done = 1;
}
