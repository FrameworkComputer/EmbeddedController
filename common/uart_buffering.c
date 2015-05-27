/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code to do UART buffering and printing */

#include <stdarg.h>

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Macros to advance in the circular buffers */
#define TX_BUF_NEXT(i) (((i) + 1) & (CONFIG_UART_TX_BUF_SIZE - 1))
#define RX_BUF_NEXT(i) (((i) + 1) & (CONFIG_UART_RX_BUF_SIZE - 1))
#define RX_BUF_PREV(i) (((i) - 1) & (CONFIG_UART_RX_BUF_SIZE - 1))

/* Macros to calculate difference of pointers in the circular buffers. */
#define TX_BUF_DIFF(i, j) (((i) - (j)) & (CONFIG_UART_TX_BUF_SIZE - 1))
#define RX_BUF_DIFF(i, j) (((i) - (j)) & (CONFIG_UART_RX_BUF_SIZE - 1))

/* ASCII control character; for example, CTRL('C') = ^C */
#define CTRL(c) ((c) - '@')

/*
 * Interval between rechecking the receive DMA head pointer, after a character
 * of input has been detected by the normal tick task.  There will be
 * CONFIG_UART_RX_DMA_RECHECKS rechecks between this tick and the next tick.
 */
#define RX_DMA_RECHECK_INTERVAL (HOOK_TICK_INTERVAL /			\
				 (CONFIG_UART_RX_DMA_RECHECKS + 1))

/* Transmit and receive buffers */
static volatile char tx_buf[CONFIG_UART_TX_BUF_SIZE];
static volatile int tx_buf_head;
static volatile int tx_buf_tail;
static volatile char rx_buf[CONFIG_UART_RX_BUF_SIZE];
static volatile int rx_buf_head;
static volatile int rx_buf_tail;
static int tx_snapshot_head;
static int tx_snapshot_tail;
static int tx_last_snapshot_head;
static int tx_next_snapshot_head;
static int uart_suspended;

/**
 * Put a single character into the transmit buffer.
 *
 * Does not enable the transmit interrupt; assumes that happens elsewhere.
 *
 * @param context	Context; ignored.
 * @param c		Character to write.
 * @return 0 if the character was transmitted, 1 if it was dropped.
 */
static int __tx_char(void *context, int c)
{
	int tx_buf_next, tx_buf_new_tail;

	/* Do newline to CRLF translation */
	if (c == '\n' && __tx_char(NULL, '\r'))
		return 1;

	tx_buf_next = TX_BUF_NEXT(tx_buf_head);
	if (tx_buf_next == tx_buf_tail)
		return 1;

	/*
	 * If we do a READ_RECENT, the buffer may have wrapped around, and
	 * we'll drop most of the logs in this case. Make sure the place
	 * we read from in that case is always ahead of the new tx_buf_head.
	 *
	 * We also want to make sure that the next time we snapshot and want
	 * to READ_RECENT, we don't start reading from a stale tail.
	 */
	tx_buf_new_tail = TX_BUF_NEXT(tx_buf_next);
	if (tx_buf_next == tx_last_snapshot_head &&
	    tx_last_snapshot_head != tx_snapshot_head)
		tx_last_snapshot_head = tx_buf_new_tail;
	if (tx_buf_next == tx_next_snapshot_head)
		tx_next_snapshot_head = tx_buf_new_tail;

	tx_buf[tx_buf_head] = c;
	tx_buf_head = tx_buf_next;
	return 0;
}

#ifdef CONFIG_UART_TX_DMA

/**
 * Process UART output via DMA
 */
void uart_process_output(void)
{
	/* Size of current DMA transfer */
	static int tx_dma_in_progress;

	/*
	 * Get head pointer now, to avoid math problems if some other task
	 * or interrupt adds output during this call.
	 */
	int head = tx_buf_head;

	if (uart_suspended)
		return;

	/* If DMA is still busy, nothing to do. */
	if (!uart_tx_dma_ready())
		return;

	/* If a previous DMA transfer completed, free up the buffer it used */
	if (tx_dma_in_progress) {
		tx_buf_tail = (tx_buf_tail + tx_dma_in_progress) &
			(CONFIG_UART_TX_BUF_SIZE - 1);
		tx_dma_in_progress = 0;
	}

	/* Disable DMA-done interrupt if nothing to send */
	if (head == tx_buf_tail) {
		uart_tx_stop();
		return;
	}

	/*
	 * Get the largest contiguous block of output.  If the transmit buffer
	 * wraps, only use the part before the wrap.
	 */
	tx_dma_in_progress = (head > tx_buf_tail ? head :
			      CONFIG_UART_TX_BUF_SIZE) - tx_buf_tail;

	uart_tx_dma_start((char *)(tx_buf + tx_buf_tail), tx_dma_in_progress);
}

#else /* !CONFIG_UART_TX_DMA */

void uart_process_output(void)
{
	if (uart_suspended)
		return;

	/* Copy output from buffer until TX fifo full or output buffer empty */
	while (uart_tx_ready() && (tx_buf_head != tx_buf_tail)) {
		uart_write_char(tx_buf[tx_buf_tail]);
		tx_buf_tail = TX_BUF_NEXT(tx_buf_tail);
	}

	/* If output buffer is empty, disable transmit interrupt */
	if (tx_buf_tail == tx_buf_head)
		uart_tx_stop();
}

#endif /* !CONFIG_UART_TX_DMA */

#ifdef CONFIG_UART_RX_DMA

void uart_process_input(void)
{
	static int fast_rechecks;
	int cur_head = rx_buf_head;

	int i;

	/* Update receive buffer head from current DMA receive pointer */
	rx_buf_head = uart_rx_dma_head();

	/* Handle software flow control characters */
	for (i = cur_head; i != rx_buf_head; i = RX_BUF_NEXT(i)) {
		int c = rx_buf[i];

#ifdef CONFIG_UART_INPUT_FILTER  /* TODO(crosbug.com/p/36745): */
#error "Filtering the UART input with DMA enabled is NOT SUPPORTED!"
#endif

		if (c == CTRL('S')) {
			/* Software flow control - XOFF */
			uart_suspended = 1;
			uart_tx_stop();
		} else if (c == CTRL('Q')) {
			/* Software flow control - XON */
			uart_suspended = 0;
			uart_tx_start();
		}
	}

	if (rx_buf_head != cur_head) {
		console_has_input();
		fast_rechecks = CONFIG_UART_RX_DMA_RECHECKS;
	}

	/*
	 * Input is checked once a tick when the console is idle.  When input
	 * is received, check more frequently for a bit, so that the console is
	 * more responsive.
	 */
	if (fast_rechecks) {
		fast_rechecks--;
		hook_call_deferred(uart_process_input, RX_DMA_RECHECK_INTERVAL);
	}
}
DECLARE_HOOK(HOOK_TICK, uart_process_input, HOOK_PRIO_DEFAULT);
DECLARE_DEFERRED(uart_process_input);

#else /* !CONFIG_UART_RX_DMA */

void uart_process_input(void)
{
	int got_input = 0;

	/* Copy input from buffer until RX fifo empty */
	while (uart_rx_available()) {
		int c = uart_read_char();
		int rx_buf_next = RX_BUF_NEXT(rx_buf_head);

#ifdef CONFIG_UART_INPUT_FILTER
		/* Intercept the input before it goes to the console */
		if (uart_input_filter(c))
			continue;
#endif

		if (c == CTRL('S')) {
			/* Software flow control - XOFF */
			uart_suspended = 1;
			uart_tx_stop();
		} else if (c == CTRL('Q')) {
			/* Software flow control - XON */
			uart_suspended = 0;
			uart_tx_start();
		} else if (rx_buf_next != rx_buf_tail) {
			/* Buffer all other input */
			rx_buf[rx_buf_head] = c;
			rx_buf_head = rx_buf_next;
			got_input = 1;
		}
	}

	if (got_input)
		console_has_input();
}

#endif /* !CONFIG_UART_RX_DMA */

int uart_putc(int c)
{
	int rv = __tx_char(NULL, c);

	if (!uart_suspended)
		uart_tx_start();

	return rv ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

int uart_puts(const char *outstr)
{
	/* Put all characters in the output buffer */
	while (*outstr) {
		if (__tx_char(NULL, *outstr++) != 0)
			break;
	}

	if (!uart_suspended)
		uart_tx_start();

	/* Successful if we consumed all output */
	return *outstr ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

int uart_vprintf(const char *format, va_list args)
{
	int rv = vfnprintf(__tx_char, NULL, format, args);

	if (!uart_suspended)
		uart_tx_start();

	return rv;
}

int uart_printf(const char *format, ...)
{
	int rv;
	va_list args;

	va_start(args, format);
	rv = uart_vprintf(format, args);
	va_end(args);
	return rv;
}

void uart_flush_output(void)
{
	/* If UART is suspended, ignore flush request. */
	if (uart_suspended)
		return;

	/* Loop until buffer is empty */
	while (tx_buf_head != tx_buf_tail) {
		if (in_interrupt_context()) {
			/*
			 * Explicitly process UART output, since the UART
			 * interrupt may not be able to preempt the interrupt
			 * we're in now.
			 */
			uart_process_output();
		} else {
			/*
			 * It's possible we switched from a previous context
			 * which was doing a printf() or puts() but hadn't
			 * enabled the UART interrupt.  Check if the interrupt
			 * is disabled, and if so, re-enable and trigger it.
			 * Note that this check is inside the while loop, so
			 * we'll be safe even if the context switches away from
			 * us to another partial printf() and back.
			 */
			uart_tx_start();
		}
	}

	/* Wait for transmit FIFO empty */
	uart_tx_flush();
}

int uart_getc(void)
{
	/* Look for a non-flow-control character */
	while (rx_buf_tail != rx_buf_head) {
		int c = rx_buf[rx_buf_tail];
		rx_buf_tail = RX_BUF_NEXT(rx_buf_tail);

		if (c != CTRL('Q') && c != CTRL('S'))
			return c;
	}

	/* If we're still here, no input */
	return -1;
}

int uart_buffer_empty(void)
{
	return tx_buf_head == tx_buf_tail;
}

#ifdef CONFIG_UART_RX_DMA
static void uart_rx_dma_init(void)
{
	/* Start receiving */
	uart_rx_dma_start((char *)rx_buf, CONFIG_UART_RX_BUF_SIZE);
}
DECLARE_HOOK(HOOK_INIT, uart_rx_dma_init, HOOK_PRIO_DEFAULT);
#endif

/*****************************************************************************/
/* Host commands */

static int host_command_console_snapshot(struct host_cmd_handler_args *args)
{
	/* Assume the whole circular buffer is full */
	tx_snapshot_head = tx_buf_head;
	tx_snapshot_tail = TX_BUF_NEXT(tx_snapshot_head);
	/* Set up pointer for just the new part of the buffer */
	tx_last_snapshot_head = tx_next_snapshot_head;
	tx_next_snapshot_head = tx_buf_head;

	/*
	 * Immediately skip any unused bytes.  This doesn't always work,
	 * because a higher-priority task or interrupt handler can write to the
	 * buffer while we're scanning it.  This is acceptable because this
	 * command is only for debugging, and the failure mode is a bit of
	 * garbage at the beginning of the saved output.  The saved buffer
	 * could also be overwritten by the head coming completely back around
	 * before we finish.  The alternative would be to make a full copy of
	 * the transmit buffer, but that requires a lot of RAM.
	 */
	while (tx_snapshot_tail != tx_snapshot_head) {
		if (tx_buf[tx_snapshot_tail])
			break;
		tx_snapshot_tail = TX_BUF_NEXT(tx_snapshot_tail);
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CONSOLE_SNAPSHOT,
		     host_command_console_snapshot,
		     EC_VER_MASK(0));

/*
 * Common code for host_command_console_read and
 * host_command_console_read_recent.
 */
static int console_read_helper(struct host_cmd_handler_args *args,
			       int *tail)
{
	char *dest = (char *)args->response;

	/* If no snapshot data, return empty response */
	if (tx_snapshot_head == *tail)
		return EC_RES_SUCCESS;

	/* Copy data to response */
	while (*tail != tx_snapshot_head &&
	       args->response_size < args->response_max - 1) {

		/*
		 * Copy only non-zero bytes, so that we don't copy unused
		 * bytes if the buffer hasn't completely rolled at boot.
		 */
		if (tx_buf[*tail]) {
			*(dest++) = tx_buf[*tail];
			args->response_size++;
		}

		*tail = TX_BUF_NEXT(*tail);
	}

	/* Null-terminate */
	*(dest++) = '\0';
	args->response_size++;

	return EC_RES_SUCCESS;
}

static int host_command_console_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_console_read_v1 *p;

	if (args->version == 0) {
		/*
		 * Prior versions of this command only support reading from
		 * an entire snapshot, not just the output since the last
		 * snapshot.
		 */
		return console_read_helper(args, &tx_snapshot_tail);
	} else if (args->version == 1) {
		/* Check the params to figure out where to start reading. */
		p = args->params;
		if (p->subcmd == CONSOLE_READ_NEXT)
			return console_read_helper(args, &tx_snapshot_tail);
		else if (p->subcmd == CONSOLE_READ_RECENT)
			return console_read_helper(args,
						   &tx_last_snapshot_head);
	}
	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_CONSOLE_READ,
		     host_command_console_read,
		     EC_VER_MASK(0) | EC_VER_MASK(1));
