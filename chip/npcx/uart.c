/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "lpc.h"
#include "registers.h"
#include "clock_chip.h"
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
	if (IS_BIT_SET(NPCX_WKEN(1, 1), 0)) {
		/* disable MIWU*/
		CLEAR_BIT(NPCX_WKEN(1, 1), 0);
		/* go back to original setting */
		task_enable_irq(NPCX_IRQ_WKINTB_1);
		/* Go back CR_SIN*/
		SET_BIT(NPCX_DEVALT(0x0A), NPCX_DEVALTA_UART_SL);
		/* enable uart again from MIWU mode */
		task_enable_irq(NPCX_IRQ_UART);
	}

	/* If interrupt is already enabled, nothing to do */
	if (NPCX_UICTRL & 0x20)
		return;

	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);

	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.  This works around a hardware problem with the
	 * UART where the FIFO only triggers the interrupt when its
	 * threshold is _crossed_, not just met.
	 */
	NPCX_UICTRL |= 0x20;

	task_trigger_irq(NPCX_IRQ_UART);
}

void uart_tx_stop(void)	/* Disable TX interrupt */
{
	NPCX_UICTRL &= ~0x20;

	/* Re-allow deep sleep */
	enable_sleep(SLEEP_MASK_UART);
}

void uart_tx_flush(void)
{
	/* Wait for transmit FIFO empty */
	while (!(NPCX_UICTRL & 0x01))
		;
}

int uart_tx_ready(void)
{
	return NPCX_UICTRL & 0x01;	/*if TX FIFO is empty return 1*/
}

int uart_tx_in_progress(void)
{
	/* Transmit is in progress if the TX busy bit is set. */
	return NPCX_USTAT & 0x40;	/*BUSY bit , if busy return 1*/
}

int uart_rx_available(void)
{
	uint8_t ctrl = NPCX_UICTRL;
#ifdef CONFIG_LOW_POWER_IDLE
	/*
	 * Activity seen on UART RX pin while UART was disabled for deep sleep.
	 * The console won't see that character because the UART is disabled,
	 * so we need to inform the clock module of UART activity ourselves.
	 */
	if (ctrl & 0x02)
		clock_refresh_console_in_use();
#endif
	return ctrl & 0x02; /* If RX FIFO is empty return '0'*/
}

void uart_write_char(char c)
{
	/* Wait for space in transmit FIFO. */
	while (!uart_tx_ready())
		;

	NPCX_UTBUF = c;
}

int uart_read_char(void)
{
	return NPCX_URBUF;
}

static void uart_clear_rx_fifo(int channel)
{
	int scratch __attribute__ ((unused));
	if (channel == 0) { /* suppose '0' is EC UART*/
		/*if '1' that mean have a RX data on the FIFO register*/
		while ((NPCX_UICTRL & 0x02))
			scratch = NPCX_URBUF;
	}
}

void uart_disable_interrupt(void)
{
	task_disable_irq(NPCX_IRQ_UART);
}

void uart_enable_interrupt(void)
{
	task_enable_irq(NPCX_IRQ_UART);
}

/**
 * Interrupt handler for UART0
 */
void uart_ec_interrupt(void)
{
	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();
}
DECLARE_IRQ(NPCX_IRQ_UART, uart_ec_interrupt, 1);


static void uart_config(void)
{
	uint32_t div, opt_dev, min_deviation, clk, calc_baudrate, deviation;
	uint8_t prescalar, opt_prescalar, i;
	/* Enable the port */
	/* Configure pins from GPIOs to CR_UART */
	gpio_config_module(MODULE_UART, 1);

	/* Calculated UART baudrate , clock source from APB2 */
	opt_prescalar = opt_dev = 0;
	prescalar = 10;
	min_deviation = 0xFFFFFFFF;
	clk = clock_get_apb2_freq();
	for (i = 1; i < 31; i++) {
		div = (clk * 10) / (16 * CONFIG_UART_BAUD_RATE * prescalar);
		if (div != 0) {
			calc_baudrate = (clk * 10) / (16 * div * prescalar);
			deviation = (calc_baudrate > CONFIG_UART_BAUD_RATE) ?
				(calc_baudrate - CONFIG_UART_BAUD_RATE) :
				(CONFIG_UART_BAUD_RATE - calc_baudrate);
			if (deviation < min_deviation) {
				min_deviation = deviation;
				opt_prescalar = i;
				opt_dev = div;
			}
		}
		prescalar += 5;
	}
	opt_dev--;
	NPCX_UPSR = ((opt_prescalar<<3) & 0xF8) | ((opt_dev >> 8) & 0x7);
	NPCX_UBAUD = (uint8_t)opt_dev;
	/*
	 * 8-N-1, FIFO enabled.  Must be done after setting
	 * the divisor for the new divisor to take effect.
	 */
	NPCX_UFRS = 0x00;
	NPCX_UICTRL = 0x40; /* receive int enable only */
}

void uart_init(void)
{
	uint32_t mask = 0;

	/*
	 * Enable UART0 in run, sleep, and deep sleep modes. Enable the Host
	 * UART in run and sleep modes.
	 */
	mask = 0x10; /* bit 4 */
	clock_enable_peripheral(CGC_OFFSET_UART, mask, CGC_MODE_ALL);

	/* Set pin-mask for UART */
	SET_BIT(NPCX_DEVALT(0x0A), NPCX_DEVALTA_UART_SL);
	gpio_config_module(MODULE_UART, 1);

	/* Configure UARTs (identically) */
	uart_config();

	/*
	 * Enable interrupts for UART0 only. Host UART will have to wait
	 * until the LPC bus is initialized.
	 */
	uart_clear_rx_fifo(0);
	task_enable_irq(NPCX_IRQ_UART);

	init_done = 1;
}
