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
#include "util.h"

#define USE_UART_INTERRUPTS (!(defined(CONFIG_CUSTOMIZED_RO) && \
			       defined(SECTION_IS_RO)))

struct uartn_interrupts {
	int tx_int;
	int rx_int;
};
static struct uartn_interrupts interrupt[] = {
	{GC_IRQNUM_UART0_TXINT, GC_IRQNUM_UART0_RXINT},
	{GC_IRQNUM_UART1_TXINT, GC_IRQNUM_UART1_RXINT},
	{GC_IRQNUM_UART2_TXINT, GC_IRQNUM_UART2_RXINT},
};

void uartn_tx_start(int uart)
{
	if (!uart_init_done())
		return;

	/* If interrupt is already enabled, nothing to do */
	if (GR_UART_ICTRL(uart) & GC_UART_ICTRL_TX_MASK)
		return;

	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);

	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.  This works around a hardware problem with the
	 * UART where the FIFO only triggers the interrupt when its
	 * threshold is _crossed_, not just met.
	 */
	/* TODO(crosbug.com/p/33819): Do we need this hack here? Find out. */
	REG_WRITE_MLV(GR_UART_ICTRL(uart), GC_UART_ICTRL_TX_MASK,
		      GC_UART_ICTRL_TX_LSB, 1);
	task_trigger_irq(interrupt[uart].tx_int);
}

void uartn_tx_stop(int uart)
{
	/* Disable the TX interrupt */
	REG_WRITE_MLV(GR_UART_ICTRL(uart), GC_UART_ICTRL_TX_MASK,
		      GC_UART_ICTRL_TX_LSB, 0);

	/* Re-allow deep sleep */
	enable_sleep(SLEEP_MASK_UART);
}

int uartn_tx_in_progress(int uart)
{
	/* Transmit is in progress unless the TX FIFO is empty and idle. */
	return (GR_UART_STATE(uart) & (GC_UART_STATE_TXIDLE_MASK |
				       GC_UART_STATE_TXEMPTY_MASK)) !=
		(GC_UART_STATE_TXIDLE_MASK | GC_UART_STATE_TXEMPTY_MASK);
}

void uartn_tx_flush(int uart)
{
	/* Wait until TX FIFO is idle. */
	while (uartn_tx_in_progress(uart))
		;
}

int uartn_tx_ready(int uart)
{
	/* True if the TX buffer is not completely full */
	return !(GR_UART_STATE(uart) & GC_UART_STATE_TX_MASK);
}

int uartn_rx_available(int uart)
{
	/* True if the RX buffer is not completely empty. */
	return !(GR_UART_STATE(uart) & GC_UART_STATE_RXEMPTY_MASK);
}

void uartn_write_char(int uart, char c)
{
	/* Wait for space in transmit FIFO. */
	while (!uartn_tx_ready(uart))
		;

	GR_UART_WDATA(uart) = c;
}

int uartn_read_char(int uart)
{
	return GR_UART_RDATA(uart);
}

void uartn_disable_interrupt(int uart)
{
	task_disable_irq(interrupt[uart].tx_int);
	task_disable_irq(interrupt[uart].rx_int);
}

void uartn_enable_interrupt(int uart)
{
	task_enable_irq(interrupt[uart].tx_int);
	task_enable_irq(interrupt[uart].rx_int);
}


void uartn_enable(int uart)
{
	/* Enable TX and RX. Disable HW flow control and loopback. */
	GR_UART_CTRL(uart) = 0x03;
}

/* Disable TX, RX, HW flow control, and loopback */
void uartn_disable(int uart)
{
	GR_UART_CTRL(uart) = 0;
}

int uartn_is_enabled(int uart)
{
	return !!(GR_UART_CTRL(uart) & 0x03);
}

void uartn_init(int uart)
{
	long long setting = (16 * BIT(UART_NCO_WIDTH) *
			     (long long)CONFIG_UART_BAUD_RATE / PCLK_FREQ);

	/* set frequency */
	GR_UART_NCO(uart) = setting;

	/*
	 * Interrupt when RX fifo has anything, when TX fifo <= half
	 * empty and reset (clear) both FIFOs
	 */
	GR_UART_FIFO(uart) = 0x63;

	/* enable RX interrupts in block */
	/* Note: doesn't do anything unless turned on in NVIC */
	GR_UART_ICTRL(uart) = 0x02;

#if USE_UART_INTERRUPTS
	/* Enable interrupts for UART */
	uartn_enable_interrupt(uart);
#endif
}
