/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for MCHP MEC */

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
#include "tfdp_chip.h"

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
	if (MCHP_UART_IER(0) & BIT(1))
		return;

	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);

	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.  This works around a hardware problem with the
	 * UART where the FIFO only triggers the interrupt when its
	 * threshold is _crossed_, not just met.
	 */
	MCHP_UART_IER(0) |= BIT(1);
	task_trigger_irq(MCHP_IRQ_UART0);
}

void uart_tx_stop(void)
{
	MCHP_UART_IER(0) &= ~BIT(1);

	/* Re-allow deep sleep */
	enable_sleep(SLEEP_MASK_UART);
}

void uart_tx_flush(void)
{
	/* Wait for transmit FIFO empty */
	while (!(MCHP_UART_LSR(0) & MCHP_LSR_TX_EMPTY))
		;
}

int uart_tx_ready(void)
{
	/*
	 * We have no indication of free space in transmit FIFO. To work around
	 * this, we check transmit FIFO empty bit every 16 characters written.
	 */
	return tx_fifo_used != 0 ||
			(MCHP_UART_LSR(0) & MCHP_LSR_TX_EMPTY);
}

int uart_tx_in_progress(void)
{
	/* return 0: FIFO is empty, 1: FIFO NOT Empty */
	return !(MCHP_UART_LSR(0) & MCHP_LSR_TX_EMPTY);
}

int uart_rx_available(void)
{
	return MCHP_UART_LSR(0) & BIT(0);
}

void uart_write_char(char c)
{
	/* Wait for space in transmit FIFO. */
	while (!uart_tx_ready())
		;

	tx_fifo_used = (tx_fifo_used + 1) % TX_FIFO_SIZE;
	MCHP_UART_TB(0) = c;
}

int uart_read_char(void)
{
	return MCHP_UART_RB(0);
}

static void uart_clear_rx_fifo(int channel)
{
	MCHP_UART_FCR(0) = BIT(0) | BIT(1);
}

void uart_disable_interrupt(void)
{
	task_disable_irq(MCHP_IRQ_UART0);
}

void uart_enable_interrupt(void)
{
	task_enable_irq(MCHP_IRQ_UART0);
}

/**
 * Interrupt handler for UART.
 * Lower priority below other critical ISR's.
 */
void uart_ec_interrupt(void)
{
	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	/* Trace statement to provide time marker for UART output? */
	uart_process_output();
}
DECLARE_IRQ(MCHP_IRQ_UART0, uart_ec_interrupt, 2);

void uart_init(void)
{
	/* Clear UART PCR sleep enable */
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_UART0);

	/* Set UART to reset on VCC1_RESET instead of nSIO_RESET */
	MCHP_UART_CFG(0) &= ~BIT(1);

	/* Baud rate = 115200. 1.8432MHz clock. Divisor = 1 */

	/* Set CLK_SRC = 0 */
	MCHP_UART_CFG(0) &= ~BIT(0);

	/* Set DLAB = 1 */
	MCHP_UART_LCR(0) |= BIT(7);

	/* PBRG0/PBRG1 */
	MCHP_UART_PBRG0(0) = 1;
	MCHP_UART_PBRG1(0) = 0;

	/* Set DLAB = 0 */
	MCHP_UART_LCR(0) &= ~BIT(7);

	/* Set word length to 8-bit */
	MCHP_UART_LCR(0) |= BIT(0) | BIT(1);

	/* Enable FIFO */
	MCHP_UART_FCR(0) = BIT(0);

	/* Activate UART */
	MCHP_UART_ACT(0) |= BIT(0);

	gpio_config_module(MODULE_UART, 1);

	/*
	 * Enable interrupts for UART0.
	 */
	uart_clear_rx_fifo(0);
	MCHP_UART_IER(0) |= BIT(0);
	MCHP_UART_MCR(0) |= BIT(3);
	MCHP_INT_ENABLE(MCHP_UART_GIRQ) = MCHP_UART_GIRQ_BIT(0);

	task_enable_irq(MCHP_IRQ_UART0);

	init_done = 1;
}

#ifdef CONFIG_LOW_POWER_IDLE
void uart_enter_dsleep(void)
{
	/* Disable the UART interrupt. */
	task_disable_irq(MCHP_IRQ_UART0);  /* NVIC interrupt for UART=13 */

	/*
	 * Set the UART0 RX pin to be a GPIO-162(fixed pin) interrupt
	 * with the flags defined in the gpio.inc file.
	 */
	gpio_reset(GPIO_UART0_RX);

	/* power-down/de-activate UART0 */
	MCHP_UART_ACT(0) &= ~BIT(0);

	/* clear interrupt enable for UART0 */
	MCHP_INT_DISABLE(MCHP_UART_GIRQ) = MCHP_UART_GIRQ_BIT(0);

	/* Clear pending interrupts on GPIO_UART0_RX(GPIO105, girq=9, bit=5) */
	MCHP_INT_SOURCE(9) = BIT(5);

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
	if (!(BIT(5) & MCHP_INT_SOURCE(9))) /* if edge interrupt */
		gpio_disable_interrupt(GPIO_UART0_RX);

	/* Configure UART0 pins for use in UART peripheral. */
	gpio_config_module(MODULE_UART, 1);

	/* Clear pending interrupts on UART peripheral and enable interrupts. */
	uart_clear_rx_fifo(0);
	MCHP_INT_SOURCE(MCHP_UART_GIRQ) = MCHP_UART_GIRQ_BIT(0);
	MCHP_INT_ENABLE(MCHP_UART_GIRQ) = MCHP_UART_GIRQ_BIT(0);
	task_enable_irq(MCHP_IRQ_UART0); /* NVIC interrupt for UART = 40 */

	/* power-up/activate UART0 */
	MCHP_UART_ACT(0) |= BIT(0);
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
