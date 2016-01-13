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

#define TX_FIFO_SIZE 16

static int init_done;
static int tx_fifo_used;

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	/* If interrupt is already enabled, nothing to do */
	if (MEC1322_UART_IER & (1 << 1))
		return;

	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);

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

	/* Re-allow deep sleep */
	enable_sleep(SLEEP_MASK_UART);
}

void uart_tx_flush(void)
{
	/* Wait for transmit FIFO empty */
	while (!(MEC1322_UART_LSR & MEC1322_LSR_TX_EMPTY))
		;
}

int uart_tx_ready(void)
{
	/*
	 * We have no indication of free space in transmit FIFO. To work around
	 * this, we check transmit FIFO empty bit every 16 characters written.
	 */
	return tx_fifo_used != 0 || (MEC1322_UART_LSR & MEC1322_LSR_TX_EMPTY);
}

int uart_tx_in_progress(void)
{
	/* return 0: FIFO is empty, 1: FIFO NOT Empty */
	return !(MEC1322_UART_LSR & MEC1322_LSR_TX_EMPTY);
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

	tx_fifo_used = (tx_fifo_used + 1) % TX_FIFO_SIZE;
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

#ifdef CONFIG_LOW_POWER_IDLE
void uart_enter_dsleep(void)
{
	/* Disable the UART interrupt. */
	task_disable_irq(MEC1322_IRQ_UART);  /* NVIC interrupt for UART=13 */

	/*
	 * Set the UART0 RX pin to be a GPIO-162(fixed pin) interrupt
	 * with the flags defined in the gpio.inc file.
	 */
	gpio_reset(GPIO_UART0_RX);

	/* power-down/de-activate UART0 */
	MEC1322_UART_ACT &= ~(1 << 0);

	/* Clear pending interrupts on GPIO_UART0_RX(GPIO162, girq=8, bit=18) */
	MEC1322_INT_SOURCE(8) = (1<<18);

	/* Enable GPIO interrupts on the UART0 RX pin. */
	gpio_enable_interrupt(GPIO_UART0_RX);
}


void uart_exit_dsleep(void)
{
	/*
	 * If the UART0 RX GPIO interrupt has not fired, then no edge has been
	 * detected. Disable the GPIO interrupt so that switching the pin over
	 * to a UART pin doesn't inadvertently cause a GPIO edge interrupt.
	 * Note: we can't disable this interrupt if it has already fired
	 * because then the IRQ will not run at all.
	 */
	if (!((1 << 18) & MEC1322_INT_SOURCE(8))) /* if edge interrupt */
		gpio_disable_interrupt(GPIO_UART0_RX);

	/* Configure UART0 pins for use in UART peripheral. */
	gpio_config_module(MODULE_UART, 1);

	/* Clear pending interrupts on UART peripheral and enable interrupts. */
	uart_clear_rx_fifo(0);
	task_enable_irq(MEC1322_IRQ_UART); /* NVIC interrupt for UART = 13 */

	/* power-up/activate UART0 */
	MEC1322_UART_ACT |= (1 << 0);
}

void uart_deepsleep_interrupt(enum gpio_signal signal)
{
	/*
	 * Activity seen on UART RX pin while UART was disabled for deep sleep.
	 * The console won't see that character because the UART is disabled,
	 * so we need to inform the clock module of UART activity ourselves.
	 */
	clock_refresh_console_in_use();

	/* Disable interrupts on UART0 RX pin to avoid repeated interrupts. */
	gpio_disable_interrupt(GPIO_UART0_RX);
}
#endif /* CONFIG_LOW_POWER_IDLE */
