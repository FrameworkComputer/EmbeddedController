/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SCP UART module */

#include "clock_chip.h"
#include "console.h"
#include "registers.h"
#include "serial_reg.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/* Console UART index */
#define UARTN           CONFIG_UART_CONSOLE
#define UART_IDLE_WAIT_US 500

static uint8_t uart_done, tx_started;

int uart_init_done(void)
{
	/*
	 * TODO: AP UART support
	 * When access AP UART port, wait for AP peripheral clock
	 */
	return uart_done;
}

void uart_tx_start(void)
{
	tx_started = 1;

	/* AP UART mode doesn't support interrupt */
	if (UARTN >= SCP_UART_COUNT)
		return;

	if (UART_IER(UARTN) & UART_IER_THRI)
		return;
	disable_sleep(SLEEP_MASK_UART);
	UART_IER(UARTN) |= UART_IER_THRI;
}

void uart_tx_stop(void)
{
	tx_started = 0;

	/* AP UART mode doesn't support interrupt */
	if (UARTN >= SCP_UART_COUNT)
		return;

	UART_IER(UARTN) &= ~UART_IER_THRI;
	enable_sleep(SLEEP_MASK_UART);
}

void uart_tx_flush(void)
{
	while (!(UART_LSR(UARTN) & UART_LSR_TEMT))
		;
}

int uart_tx_ready(void)
{
	/* Check xmit FIFO empty */
	return UART_LSR(UARTN) & UART_LSR_THRE;
}

int uart_rx_available(void)
{
	/* Check rcvr data ready */
	return UART_LSR(UARTN) & UART_LSR_DR;
}

void uart_write_char(char c)
{
	while (!uart_tx_ready())
		;

	UART_DATA(UARTN) = c;
}

int uart_read_char(void)
{
	return UART_DATA(UARTN);
}

void uart_process(void)
{
	uart_process_input();
	uart_process_output();
}

#if (UARTN < SCP_UART_COUNT)
void uart_interrupt(void)
{
	uint8_t ier;

	task_clear_pending_irq(UART_IRQ(UARTN));
	uart_process();
	ier = UART_IER(UARTN);
	UART_IER(UARTN) = 0;
	UART_IER(UARTN) = ier;
}
DECLARE_IRQ(UART_IRQ(UARTN), uart_interrupt, 2);

void uart_rx_interrupt(void)
{
	uint8_t ier;

	task_clear_pending_irq(UART_RX_IRQ(UARTN));
	SCP_INTC_UART_RX_IRQ &= ~BIT(UARTN);
	uart_process();
	ier = UART_IER(UARTN);
	UART_IER(UARTN) = 0;
	UART_IER(UARTN) = ier;
	SCP_INTC_UART_RX_IRQ |= 1 << UARTN;
}
DECLARE_IRQ(UART_RX_IRQ(UARTN), uart_rx_interrupt, 2);
#endif

void uart_task(void)
{
#if (UARTN >= SCP_UART_COUNT)
	while (1) {
		if (uart_rx_available() || tx_started)
			uart_process();
		else
			task_wait_event(UART_IDLE_WAIT_US);
	}
#endif
}

void uart_init(void)
{
	const uint32_t baud_rate = CONFIG_UART_BAUD_RATE;
	/*
	 * UART clock source is set to ULPOSC1 / 10 below.
	 *
	 * TODO(b:134035444): We could get slightly more precise frequency by
	 * using the _measured_ ULPOSC1 frequency (instead of the target).
	 */
	const uint32_t uart_clock = ULPOSC1_CLOCK_MHZ * 1000 / 10 * 1000;
	const uint32_t div = DIV_ROUND_NEAREST(uart_clock, baud_rate * 16);

	/* Init clock */
#if UARTN == 0
	SCP_CLK_UART = CLK_UART_SEL_ULPOSC1_DIV10;
	SCP_CLK_GATE |= CG_UART_M | CG_UART_B | CG_UART_RSTN;
#elif UARTN == 1
	SCP_CLK_UART = CLK_UART1_SEL_ULPOSC1_DIV10;
	SCP_CLK_GATE |= CG_UART1_M | CG_UART1_B | CG_UART1_RSTN;
#endif

	/* Init and clear FIFO */
	UART_FCR(UARTN) = UART_FCR_ENABLE_FIFO
		| UART_FCR_CLEAR_RCVR
		| UART_FCR_CLEAR_XMIT;
	/* Line control: parity none, 8 bit, 1 stop bit */
	UART_LCR(UARTN) = UART_LCR_WLEN8;
	/* For baud rate <= 115200 */
	UART_HIGHSPEED(UARTN) = 0;
	/* DLAB = 1 and update DLL DLH */
	UART_LCR(UARTN) |= UART_LCR_DLAB;
	UART_DLL(UARTN) = div & 0xff;
	UART_DLH(UARTN) = (div >> 8) & 0xff;
	UART_LCR(UARTN) &= ~UART_LCR_DLAB;
	UART_IER(UARTN) |= UART_IER_RDI;

#if (UARTN < SCP_UART_COUNT)
	task_enable_irq(UART_IRQ(UARTN));
	task_enable_irq(UART_RX_IRQ(UARTN));
	/* UART RX IRQ needs an extra enable */
	SCP_INTC_UART_RX_IRQ |= 1 << UARTN;
#endif
	gpio_config_module(MODULE_UART, 1);
	uart_done = 1;
}
