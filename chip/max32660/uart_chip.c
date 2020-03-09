/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 Console UART Module for Chrome EC */

#include <stdint.h>
#include "system.h"
#include "task.h"
#include "uart.h"
#include "registers.h"
#include "tmr_regs.h"
#include "gpio_regs.h"
#include "common.h"
#include "gcr_regs.h"
#include "uart_regs.h"

static int done_uart_init_yet;

#ifndef UARTN
#define UARTN CONFIG_UART_HOST
#endif

#if (UARTN == 0)
#define MXC_UART MXC_UART0
#define EC_UART_IRQn EC_UART0_IRQn
#elif (UARTN == 1)
#define MXC_UART MXC_UART1
#define EC_UART_IRQn EC_UART1_IRQn
#else
#error "MAX32660 supports only UART 0 or 1 for EC console"
#endif

#define UART_BAUD 115200

#define UART_ER_IF                                                             \
	(MXC_F_UART_INT_FL_RX_FRAME_ERROR |                                    \
	 MXC_F_UART_INT_FL_RX_PARITY_ERROR | MXC_F_UART_INT_FL_RX_OVERRUN)

#define UART_ER_IE                                                             \
	(MXC_F_UART_INT_EN_RX_FRAME_ERROR |                                    \
	 MXC_F_UART_INT_EN_RX_PARITY_ERROR | MXC_F_UART_INT_EN_RX_OVERRUN)

#define UART_RX_IF (UART_ER_IF | MXC_F_UART_INT_FL_RX_FIFO_THRESH)

#define UART_RX_IE (UART_ER_IE | MXC_F_UART_INT_EN_RX_FIFO_THRESH)

#define UART_TX_IF                                                             \
	(UART_ER_IF | MXC_F_UART_INT_FL_TX_FIFO_ALMOST_EMPTY |                 \
	 MXC_F_UART_INT_FL_TX_FIFO_THRESH)

#define UART_TX_IE                                                             \
	(UART_ER_IE | MXC_F_UART_INT_EN_TX_FIFO_ALMOST_EMPTY |                 \
	 MXC_F_UART_INT_EN_TX_FIFO_THRESH)

#define UART_RX_THRESHOLD_LEVEL 1

/**
 * Alternate clock rate. (7.3728MHz) */
#define UART_ALTERNATE_CLOCK_HZ 7372800

/* ************************************************************************* */
static unsigned int uart_number_write_available(mxc_uart_regs_t *uart)
{
	return MXC_UART_FIFO_DEPTH -
	       ((uart->status & MXC_F_UART_STATUS_TX_FIFO_CNT) >>
		MXC_F_UART_STATUS_TX_FIFO_CNT_POS);
}

/* ************************************************************************* */
static unsigned int uart_number_read_available(mxc_uart_regs_t *uart)
{
	return ((uart->status & MXC_F_UART_STATUS_RX_FIFO_CNT) >>
		MXC_F_UART_STATUS_RX_FIFO_CNT_POS);
}

static void uartn_enable_tx_interrupt(int uart_num)
{
	// Enable the interrupts
	MXC_UART_GET_UART(uart_num)->int_en |= UART_TX_IE;
}

static void uartn_disable_tx_interrupt(int uart_num)
{
	// Disable the interrupts
	MXC_UART_GET_UART(uart_num)->int_en &= ~UART_TX_IE;
}

static int uartn_tx_in_progress(int uart_num)
{
	return ((MXC_UART_GET_UART(uart_num)->status &
		 (MXC_F_UART_STATUS_TX_BUSY)) != 0);
}

static void uartn_tx_flush(int uart_num)
{
	while (uartn_tx_in_progress(uart_num)) {
	}
}

static int uartn_tx_ready(int uart_num)
{
	int avail;
	avail = uart_number_write_available(MXC_UART_GET_UART(uart_num));
	/* True if the TX buffer is not completely full */
	return (avail != 0);
}

static int uartn_rx_available(int uart_num)
{
	int avail;
	/* True if the RX buffer is not completely empty. */
	avail = uart_number_read_available(MXC_UART_GET_UART(uart_num));
	return (avail != 0);
}

static void uartn_write_char(int uart_num, char c)
{
	int avail;
	mxc_uart_regs_t *uart;

	uart = MXC_UART_GET_UART(uart_num);
	/* Refill the TX FIFO */
	avail = uart_number_write_available(uart);

	/* wait until there is room in the fifo */
	while (avail == 0) {
		avail = uart_number_write_available(uart);
	}

	/* stuff the fifo with the character */
	uart->fifo = c;
}

static int uartn_read_char(int uart_num)
{
	int c;
	c = MXC_UART_GET_UART(uart_num)->fifo;
	return c;
}

static void uartn_clear_interrupt_flags(int uart_num)
{
	uint32_t flags;
	// Read and clear interrupts
	//    intst = MXC_UART_GET_UART(uart_num)->int_fl;
	//    MXC_UART_GET_UART(uart_num)->int_fl = ~intst;

	flags = MXC_UART_GET_UART(uart_num)->int_fl;
	MXC_UART_GET_UART(uart_num)->int_fl = flags;
}

static inline int uartn_is_rx_interrupt(int uart_num)
{
	return MXC_UART_GET_UART(uart_num)->int_fl & UART_RX_IF;
}

static inline int uartn_is_tx_interrupt(int uart_num)
{
	return MXC_UART_GET_UART(uart_num)->int_fl & UART_TX_IF;
}

int uart_init_done(void)
{
	return done_uart_init_yet;
}

void uart_tx_start(void)
{
	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);
	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.
	 */
	uartn_enable_tx_interrupt(UARTN);
	task_trigger_irq(EC_UART_IRQn);
}

void uart_tx_stop(void)
{
	uartn_disable_tx_interrupt(UARTN);
	/* Re-allow deep sleep */
	enable_sleep(SLEEP_MASK_UART);
}

int uart_tx_in_progress(void)
{
	return uartn_tx_in_progress(UARTN);
}

void uart_tx_flush(void)
{
	uartn_tx_flush(UARTN);
}

int uart_tx_ready(void)
{
	/* True if the TX buffer is not completely full */
	return uartn_tx_ready(UARTN);
}

int uart_rx_available(void)
{
	/* True if the RX buffer is not completely empty. */
	return uartn_rx_available(UARTN);
}

void uart_write_char(char c)
{
	/* write a character to the UART */
	uartn_write_char(UARTN, c);
}

int uart_read_char(void)
{
	return uartn_read_char(UARTN);
}

/**
 * Interrupt handlers for UART
 */
void uart_rxtx_interrupt(void)
{
	/* Process the Console Input */
	uart_process_input();
	/* Process the Buffered Console Output */
	uart_process_output();
	uartn_clear_interrupt_flags(UARTN);
}
DECLARE_IRQ(EC_UART_IRQn, uart_rxtx_interrupt, 1);

void uart_init(void)
{
	uint32_t flags;
	uint32_t baud0 = 0, baud1 = 0, div;
	int32_t factor = -1;

	/* Init the GPIO Port Mapping */
	gpio_config_module(MODULE_UART, 1);

	/* Drain FIFOs and enable UART and set configuration */
	MXC_UART->ctrl = (MXC_F_UART_CTRL_ENABLE | MXC_S_UART_CTRL_CHAR_SIZE_8 | 1);

	/* Set the baud rate */
	div = PeripheralClock / (UART_BAUD); 	// constant part of DIV (i.e. DIV
					     					// * (Baudrate*factor_int))

	do {
		factor += 1;
		baud0 = div >> (7 - factor); 	// divide by 128,64,32,16 to
					     				// extract integer part
		baud1 = ((div << factor) -
			 (baud0 << 7)); 			// subtract factor corrected div -
										// integer parts

	} while ((baud0 == 0) && (factor < 4));

	MXC_UART->baud0 = ((factor << MXC_F_UART_BAUD0_FACTOR_POS) | baud0);
	MXC_UART->baud1 = baud1;

	MXC_UART->thresh_ctrl = UART_RX_THRESHOLD_LEVEL
			    << MXC_F_UART_THRESH_CTRL_RX_FIFO_THRESH_POS;

	/* Clear Interrupt Flags */
	flags = MXC_UART->int_fl;
	MXC_UART->int_fl = flags;

	/* Enable the RX interrupts */
	MXC_UART->int_en |= UART_RX_IE;

	/* Enable the IRQ */
	task_enable_irq(EC_UART_IRQn);
	/* Set a flag for the system that the UART has been initialized */
	done_uart_init_yet = 1;
}
