/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Rotor MCU */

#include "gpio.h"
#include "registers.h"
#include "task.h"
#include "uart.h"

/* UART reference clock is 200MHz. */
#define UART_REF_CLK 200000000

static int init_done;


void uart_init(void)
{
	uint16_t divisor;

	/* Ungate the UART clock and the UART reference clock. */
	ROTOR_MCU_UART0_CLKGEN |= (1 << 1);
	ROTOR_MCU_UART0_REFCLKGEN |= (1 << 1);

	/* Set DLAB = 1 */
	ROTOR_MCU_UART_LCR(0) |= (1 << 7);

	/* Set divisor for baud rate. */
#ifdef BOARD_REI
	divisor = 1;
#else
	divisor = UART_REF_CLK / (16 * CONFIG_UART_BAUD_RATE);
#endif /* !defined(BOARD_REI) */
	ROTOR_MCU_UART_DLH(0) = (divisor & 0xFF00) >> 8;
	ROTOR_MCU_UART_DLL(0) = divisor & 0xFF;

	/* Clear DLAB bit. */
	ROTOR_MCU_UART_LCR(0) &= ~(1 << 7);

	/* Set data bits per character to 8. */
	ROTOR_MCU_UART_LCR(0) |= (1 << 1) | (1 << 0);

	/* Enable FIFOs. */
	ROTOR_MCU_UART_FCR(0) |= (1 << 0);

	/* Enable the pins for UART functionality. */
	gpio_config_module(MODULE_UART, 1);

	/* Enable Received Data Available Interrupt. */
	ROTOR_MCU_UART_IER(0) |= (1 << 0);

	/* Enable the interrupt. */
	task_enable_irq(ROTOR_MCU_IRQ_UART_0);

	init_done = 1;
}

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_flush(void)
{
	/* Wait for TX FIFO to empty. */
	while (!(ROTOR_MCU_UART_USR(0) & (1 << 2)))
		;
}

int uart_tx_ready(void)
{
	/* Bit set if TX FIFO is not full. */
	return (ROTOR_MCU_UART_LSR(0) & (1 << 5));

}

int uart_tx_in_progress(void)
{
	/* Bit set if TX FIFO is empty. */
	return !(ROTOR_MCU_UART_USR(0) & (1 << 2));
}

int uart_rx_available(void)
{
	/* Bit set if RX FIFO is not empty. */
	return (ROTOR_MCU_UART_LSR(0) & (1 << 0));
}

void uart_write_char(char c)
{
	/* Wait for space in TX FIFO. */
	while (!uart_tx_ready())
		;

	ROTOR_MCU_UART_THR(0) = c;
}

int uart_read_char(void)
{
	return ROTOR_MCU_UART_RBR(0);
}

void uart_disable_interrupt(void)
{
	task_disable_irq(ROTOR_MCU_IRQ_UART_0);
}

void uart_enable_interrupt(void)
{
	task_enable_irq(ROTOR_MCU_IRQ_UART_0);
}

void uart_tx_start(void)
{
	/* Nothing to do if the interrupt is already enabled. */
	if (ROTOR_MCU_UART_IER(0) & (1 << 1))
		return;

	ROTOR_MCU_UART_IER(0) |= (1 << 1);
	/* Force trigger a UART interrupt. */
	task_trigger_irq(ROTOR_MCU_IRQ_UART_0);
}

void uart_tx_stop(void)
{
	/* Disable Transmit Holding Register Empty interrupt. */
	ROTOR_MCU_UART_IER(0) &= ~(1 << 1);
}

void uart_ec_interrupt(void)
{
	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();
}
DECLARE_IRQ(ROTOR_MCU_IRQ_UART_0, uart_ec_interrupt, 1);
