/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code to do UART buffering and printing */

#include <stdarg.h>

#include "console.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/* Buffer sizes; should be power of 2 */
#define TX_BUF_SIZE 512
#define RX_BUF_SIZE 128  /* suggest larger than 80 to copy&paste script. */

/* Macros to advance in the circular transmit and receive buffers */
#define TX_BUF_NEXT(i) (((i) + 1) & (TX_BUF_SIZE - 1))
#define RX_BUF_NEXT(i) (((i) + 1) & (RX_BUF_SIZE - 1))
#define RX_BUF_PREV(i) (((i) - 1) & (RX_BUF_SIZE - 1))

/* Transmit and receive buffers */
static volatile char tx_buf[TX_BUF_SIZE];
static volatile int tx_buf_head;
static volatile int tx_buf_tail;
static volatile char rx_buf[RX_BUF_SIZE];
static volatile int rx_buf_head;
static volatile int rx_buf_tail;
static int last_rx_was_cr;
static int in_escape;

static int console_mode = 1;

/* TODO: should have an API to set raw mode for the UART.  In raw
 * mode, we don't do CRLF translation or echo input. */


/* Put a single character into the transmit buffer.  Does not enable
 * the transmit interrupt; assumes that happens elsewhere.  Returns
 * zero if the character was transmitted, 1 if it was dropped. */
static int __tx_char(int c)
{
	int tx_buf_next;

	/* Do newline to CRLF translation */
	if (console_mode && c == '\n' && __tx_char('\r'))
		return 1;

	tx_buf_next = TX_BUF_NEXT(tx_buf_head);
	if (tx_buf_next == tx_buf_tail)
		return 1;

	tx_buf[tx_buf_head] = c;
	tx_buf_head = tx_buf_next;
	return 0;
}

/* Helper for UART processing */
void uart_process(void)
{
	/* Copy input from buffer until RX fifo empty */
	while (uart_rx_available()) {
		int c = uart_read_char();

		/* Handle console mode echoing and translation */
		if (console_mode) {
			/* Translate CR and CRLF to LF (newline) */
			if (c == '\r') {
				last_rx_was_cr = 1;
				c = '\n';
			} else if (c == '\n' && last_rx_was_cr) {
				last_rx_was_cr = 0;
				continue;
			} else {
				last_rx_was_cr = 0;
			}

			/* Eat common terminal escape sequences (ESC [ ...).
			 * Would be really cool if we used arrow keys to edit
			 * command history, but for now it's sufficient just to
			 * keep them from causing problems. */
			if (c == 0x1B) {
				in_escape = 1;
				continue;
			} else if (in_escape) {
				if (isalpha(c) || c == '~')
					in_escape = 0;
				continue;
			}

			/* Echo characters directly to the transmit FIFO so we
			 * don't interfere with the transmit buffer.  This
			 * means that if a lot of output is happening, input
			 * characters won't always be properly echoed. */
			if (c == '\n')
				uart_write_char('\r');
			uart_write_char(c);

			/* Handle backspace if we can */
			if (c == '\b') {
				if (rx_buf_head != rx_buf_tail) {
					/* Delete the previous character (and
					 * space over it on the output) */
					uart_write_char(' ');
					uart_write_char('\b');
					rx_buf_head = RX_BUF_PREV(rx_buf_head);
				}
				continue;
			}
		}

		rx_buf[rx_buf_head] = c;
		rx_buf_head = RX_BUF_NEXT(rx_buf_head);
		/* On overflow, discard oldest output */
		if (rx_buf_head == rx_buf_tail)
			rx_buf_tail = RX_BUF_NEXT(rx_buf_tail);

		/* Call console callback on newline, if in console mode */
		if (console_mode && c == '\n')
			console_has_input();
	}

	/* Copy output from buffer until TX fifo full or output buffer empty */
	while (uart_tx_ready() && (tx_buf_head != tx_buf_tail)) {
		uart_write_char(tx_buf[tx_buf_tail]);
		tx_buf_tail = TX_BUF_NEXT(tx_buf_tail);
	}

	/* If output buffer is empty, disable transmit interrupt */
	if (tx_buf_tail == tx_buf_head)
		uart_tx_stop();
}

void uart_set_console_mode(int enable)
{
	console_mode = enable;
}


int uart_puts(const char *outstr)
{
	int was_empty = (tx_buf_head == tx_buf_tail);

	/* Put all characters in the output buffer */
	while (*outstr) {
		if (__tx_char(*outstr++) != 0)
			break;
	}

	if (was_empty)
		uart_tx_start();

	/* Successful if we consumed all output */
	return *outstr ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}


int uart_printf(const char *format, ...)
{
	static const char int_chars[] = "0123456789abcdef";
	static const char error_str[] = "ERROR";
	char intbuf[21];  /* Longest uint64 */
	int dropped_chars = 0;
	int is_left;
	int pad_zero;
	int pad_width;
	int was_empty = (tx_buf_head == tx_buf_tail);
	va_list args;
	char *vstr;
	int vlen;

	va_start(args, format);

	while (*format && !dropped_chars) {
		int c = *format++;

		/* Copy normal characters */
		if (c != '%') {
			dropped_chars |= __tx_char(c);
			continue;
		}

		/* Get first format character */
		c = *format++;

		/* Send "%" for "%%" input */
		if (c == '%' || c == '\0') {
			dropped_chars |= __tx_char('%');
			continue;
		}

		/* Handle %c */
		if (c == 'c') {
			c = va_arg(args, int);
			dropped_chars |= __tx_char(c);
			continue;
		}

		/* Handle left-justification ("%-5s") */
		is_left = (c == '-');
		if (is_left)
			c = *format++;

		/* Handle padding with 0's */
		pad_zero = (c == '0');
		if (pad_zero)
			c = *format++;

		/* Count padding length */
		pad_width = 0;
		while (c >= '0' && c <= '9') {
			pad_width = (10 * pad_width) + c - '0';
			c = *format++;
		}
		if (pad_width > 80) {
			/* Sanity check for width failed */
			format = error_str;
			continue;
		}

		if (c == 's') {
			vstr = va_arg(args, char *);
			if (vstr == NULL)
				vstr = "(NULL)";
		} else {
			uint32_t v;
			int is_negative = 0;
			int base = 10;

			/* TODO: (crosbug.com/p/7490) handle "%l" prefix for
			 * uint64_t */

			v = va_arg(args, uint32_t);

			switch (c) {
			case 'd':
				if ((int)v < 0) {
					is_negative = 1;
					v = -v;
				}
				break;
			case 'u':
				break;
			case 'x':
			case 'p':
					base = 16;
			break;
			default:
				format = error_str;
			}
			if (format == error_str)
				continue; /* Bad format specifier */

			/* Convert integer to string, starting at end of
			 * buffer and working backwards. */
			vstr = intbuf + sizeof(intbuf) - 1;
			*(vstr) = '\0';

			if (!v)
				*(--vstr) = '0';

			while (v) {
				*(--vstr) = int_chars[v % base];
				v /= base;
			}
			if (is_negative)
				*(--vstr) = '-';
		}

		/* Copy string (or stringified integer) */
		vlen = strlen(vstr);
		while (vlen < pad_width && !is_left) {
			dropped_chars |= __tx_char(pad_zero ? '0' : ' ');
			vlen++;
		}
		while (*vstr)
			dropped_chars |= __tx_char(*vstr++);
		while (vlen < pad_width && is_left) {
			dropped_chars |= __tx_char(' ');
			vlen++;
		}
	}
	va_end(args);

	if (was_empty)
		uart_tx_start();

	/* Successful if we consumed all output */
	return dropped_chars ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

void uart_flush_output(void)
{
	/* Wait for buffer to empty */
	while (tx_buf_head != tx_buf_tail) {
		/* It's possible we're in some other interrupt, and the
		 * previous context was doing a printf() or puts() but hadn't
		 * enabled the UART interrupt.  Check if the interrupt is
		 * disabled, and if so, re-enable and trigger it.  Note that
		 * this check is inside the while loop, so we'll be safe even
		 * if the context switches away from us to another partial
		 * printf() and back. */
		if (uart_tx_stopped())
			uart_tx_start();
	}

	/* Wait for transmit FIFO empty */
	uart_tx_flush();
}

void uart_emergency_flush(void)
{
	do {
		/* Copy output from buffer until TX fifo full
		 * or output buffer empty
		 */
		while (uart_tx_ready() &&
		       (tx_buf_head != tx_buf_tail)) {
			uart_write_char(tx_buf[tx_buf_tail]);
			tx_buf_tail = TX_BUF_NEXT(tx_buf_tail);
		}
		/* Wait for transmit FIFO empty */
		uart_tx_flush();
	} while (tx_buf_head != tx_buf_tail);
}


void uart_flush_input(void)
{
	/* Disable interrupts */
	uart_disable_interrupt();

	/* Empty the hardware FIFO */
	uart_process();

	/* Clear the input buffer */
	rx_buf_tail = rx_buf_head;

	/* Re-enable interrupts */
	uart_enable_interrupt();
}


int uart_peek(int c)
{
	int index = -1;
	int i = 0;

	/* Disable interrupts while we pull characters out, because the
	 * interrupt handler can also modify the tail pointer. */
	uart_disable_interrupt();

	/* Call interrupt handler to empty the hardware FIFO.  The minimum
	 * FIFO trigger depth is 1/8 (2 chars), so this is the only way to
	 * ensure we've pulled the very last character out of the FIFO. */
	uart_process();

	for (i = rx_buf_tail; i != rx_buf_head; i = RX_BUF_NEXT(i)) {
		if (rx_buf[i] == c) {
			index = (RX_BUF_SIZE + i - rx_buf_tail) &
					(RX_BUF_SIZE - 1);
			break;
		}
	}

	/* Re-enable interrupts */
	uart_enable_interrupt();

	return index;
}


int uart_getc(void)
{
	int c;

	/* Disable interrupts */
	uart_disable_interrupt();

	/* Call interrupt handler to empty the hardware FIFO */
	uart_process();

	if (rx_buf_tail == rx_buf_head) {
		c = -1;  /* No pending input */
	} else {
		c = rx_buf[rx_buf_tail];
		rx_buf_tail = RX_BUF_NEXT(rx_buf_tail);
	}

	/* Re-enable interrupts */
	uart_enable_interrupt();

	return c;
}


int uart_gets(char *dest, int size)
{
	int got = 0;
	int c;

	/* Disable interrupts while we pull characters out, because the
	 * interrupt handler can also modify the tail pointer. */
	uart_disable_interrupt();

	/* Call interrupt handler to empty the hardware FIFO */
	uart_process();

	/* Read characters */
	while (got < size - 1 && rx_buf_tail != rx_buf_head) {
		c = rx_buf[rx_buf_tail];
		dest[got++] = c;
		rx_buf_tail = RX_BUF_NEXT(rx_buf_tail);
		if (c == '\n')
			break;  /* Stop on newline */
	}

	/* Re-enable interrupts */
	uart_enable_interrupt();

	/* Null-terminate */
	dest[got] = '\0';

	/* Return the length we got */
	return got;
}
