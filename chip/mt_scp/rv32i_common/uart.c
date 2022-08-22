/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SCP UART module */

#include "csr.h"
#include "system.h"
#include "uart.h"
#include "uart_regs.h"
#include "util.h"

/*
 * UARTN == 0, SCP UART0
 * UARTN == 1, SCP UART1
 * UARTN == 2, AP UART1
 */
#define UARTN CONFIG_UART_CONSOLE
#define UART_IDLE_WAIT_US 500
#define UART_INTC_GROUP 12

static uint8_t init_done, tx_started;

void uart_init(void)
{
	const uint32_t baud_rate = CONFIG_UART_BAUD_RATE;
	const uint32_t uart_clock = 26000000;
	const uint32_t div = DIV_ROUND_NEAREST(uart_clock, baud_rate * 16);

	uart_init_pinmux();

	/* Clear FIFO */
	UART_FCR(UARTN) = UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR |
			  UART_FCR_CLEAR_XMIT;
	/* Line control: parity none, 8 bit, 1 stop bit */
	UART_LCR(UARTN) = UART_LCR_WLEN8;
	/* For baud rate <= 115200 */
	UART_HIGHSPEED(UARTN) = 0;

	/* DLAB start */
	UART_LCR(UARTN) |= UART_LCR_DLAB;
	UART_DLL(UARTN) = div & 0xff;
	UART_DLH(UARTN) = (div >> 8) & 0xff;
	UART_LCR(UARTN) &= ~UART_LCR_DLAB;
	/* DLAB end */

	/* Enable received data interrupt */
	UART_IER(UARTN) |= UART_IER_RDI;

#if (UARTN < SCP_UART_COUNT)
	task_enable_irq(UART_TX_IRQ(UARTN));
	task_enable_irq(UART_RX_IRQ(UARTN));
#endif

	init_done = 1;
}

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_flush(void)
{
	while (!(UART_LSR(UARTN) & UART_LSR_TEMT))
		;
}

int uart_tx_ready(void)
{
	return UART_LSR(UARTN) & UART_LSR_THRE;
}

int uart_rx_available(void)
{
	return UART_LSR(UARTN) & UART_LSR_DR;
}

void uart_write_char(char c)
{
	while (!uart_tx_ready())
		;

	UART_THR(UARTN) = c;
}

int uart_read_char(void)
{
	return UART_RBR(UARTN);
}

void uart_tx_start(void)
{
	tx_started = 1;
	if (UART_IER(UARTN) & UART_IER_THRI)
		return;
	disable_sleep(SLEEP_MASK_UART);
	UART_IER(UARTN) |= UART_IER_THRI;
}

void uart_tx_stop(void)
{
	/*
	 * Workaround for b/157541273.
	 * Don't unset the THRI flag unless we are in the UART ISR.
	 *
	 * Note:
	 * MICAUSE denotes current INTC group number.
	 */
	if (in_interrupt_context() &&
	    read_csr(CSR_VIC_MICAUSE) != UART_INTC_GROUP)
		return;

	tx_started = 0;
	UART_IER(UARTN) &= ~UART_IER_THRI;
	enable_sleep(SLEEP_MASK_UART);
}

static void uart_process(void)
{
	uart_process_input();
	uart_process_output();
}

#if (UARTN < SCP_UART_COUNT)
static void uart_irq_handler(void)
{
	extern volatile int ec_int;

	switch (ec_int) {
	case UART_TX_IRQ(UARTN):
		uart_process();
		task_clear_pending_irq(ec_int);
		break;
	case UART_RX_IRQ(UARTN):
		uart_process();
		SCP_CORE_INTC_UART_RX_IRQ(UARTN) = BIT(0);
		asm volatile("fence.i" ::: "memory");
		task_clear_pending_irq(ec_int);
		break;
	}
}
DECLARE_IRQ(UART_INTC_GROUP, uart_irq_handler, 0);
#else

#ifndef HAS_TASK_APUART
#error "APUART task hasn't defined in ec.tasklist."
#endif

void uart_task(void)
{
	while (1) {
		if (uart_rx_available() || tx_started)
			uart_process();
		else
			task_wait_event(UART_IDLE_WAIT_US);
	}
}
#endif /* UARTN < SCP_UART_COUNT */
