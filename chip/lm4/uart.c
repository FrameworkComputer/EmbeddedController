/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC */

#include <stdarg.h>

#include "board.h"
#include "console.h"
#include "lpc.h"
#include "registers.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/* Baud rate for UARTs */
#define BAUD_RATE 115200

/* Buffer sizes; should be power of 2 */
#define TX_BUF_SIZE 512
#define RX_BUF_SIZE 128  /* suggest larger than 80 to copy&paste script. */

/* Macros to advance in the circular transmit and receive buffers */
#define TX_BUF_NEXT(i) (((i) + 1) & (TX_BUF_SIZE - 1))
#define RX_BUF_NEXT(i) (((i) + 1) & (RX_BUF_SIZE - 1))
#define RX_BUF_PREV(i) (((i) - 1) & (RX_BUF_SIZE - 1))

/* Transmit and receive buffers */
static volatile char tx_buf[TX_BUF_SIZE];
static volatile int tx_buf_head = 0;
static volatile int tx_buf_tail = 0;
static volatile char rx_buf[RX_BUF_SIZE];
static volatile int rx_buf_head = 0;
static volatile int rx_buf_tail = 0;
static int last_rx_was_cr = 0;

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


/* Interrupt handler for UART0 */
static void uart_0_interrupt(void)
{
	/* Clear transmit and receive interrupt status */
	LM4_UART_ICR(0) = 0x70;

	/* Copy input from buffer until RX fifo empty */
	while (!(LM4_UART_FR(0) & 0x10)) {
		int c = LM4_UART_DR(0);

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

			/* Echo characters directly to the transmit FIFO so we
			 * don't interfere with the transmit buffer.  This
			 * means that if a lot of output is happening, input
			 * characters won't always be properly echoed. */
			if (console_mode && c == '\n')
				LM4_UART_DR(0) = '\r';
			LM4_UART_DR(0) = c;

			/* Handle backspace if we can */
			if (c == '\b') {
				if (rx_buf_head != rx_buf_tail) {
					/* Delete the previous character (and
					 * space over it on the output) */
					LM4_UART_DR(0) = ' ';
					LM4_UART_DR(0) = '\b';
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
	while (!(LM4_UART_FR(0) & 0x20) && (tx_buf_head != tx_buf_tail)) {
		LM4_UART_DR(0) = tx_buf[tx_buf_tail];
		tx_buf_tail = TX_BUF_NEXT(tx_buf_tail);
	}

	/* If output buffer is empty, disable transmit interrupt */
	if (tx_buf_tail == tx_buf_head)
		LM4_UART_IM(0) &= ~0x20;
}
DECLARE_IRQ(LM4_IRQ_UART0, uart_0_interrupt, 1);


/* Interrupt handler for UART1 */
static void uart_1_interrupt(void)
{
	/* Clear transmit and receive interrupt status */
	LM4_UART_ICR(1) = 0x70;

	/* TODO: (crosbug.com/p/7488) handle input */

	/* If we have space in our FIFO and a character is pending in LPC,
	 * handle that character. */
	if (!(LM4_UART_FR(1) & 0x20) && lpc_comx_has_char()) {
		/* Copy the next byte then disable transmit interrupt */
		LM4_UART_DR(1) = lpc_comx_get_char();
		LM4_UART_IM(1) &= ~0x20;
	}
}
/* Must be same prio as LPC interrupt handler so they don't preempt */
DECLARE_IRQ(LM4_IRQ_UART1, uart_1_interrupt, 2);


/* Configure GPIOs for the UART module. */
/* TODO: board-dependent; on Link UART1 is PC4/PC5, not PB0/PB1. */
static void configure_gpio(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable clocks to GPIO blocks A and B, then delay a few clocks. */
	LM4_SYSTEM_RCGCGPIO |= 0x0003;

	/* UART0 setup; RX and TX are GPIO PA0 and PA1 */
	/* Enable alternate function */
	LM4_GPIO_AFSEL(LM4_GPIO_A) |= 0x03;
	/* Alternate function 1 */
	LM4_GPIO_PCTL(LM4_GPIO_A) = (LM4_GPIO_PCTL(LM4_GPIO_A) & 0xffffff00)
		| 0x11;
	/* Enable digital function */
	LM4_GPIO_DEN(LM4_GPIO_A) |= 0x03;

	/* UART1 setup; RX and TX are GPIO PB0 and PB1 */
	/* Enable alternate function */
	LM4_GPIO_AFSEL(LM4_GPIO_B) |= 0x03;
	/* Alternate function 1 */
	LM4_GPIO_PCTL(LM4_GPIO_B) = (LM4_GPIO_PCTL(LM4_GPIO_B) & 0xffffff00)
		| 0x11;
	/* Enable digital function */
	LM4_GPIO_DEN(LM4_GPIO_B) = 0x03;
}


int uart_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));
	int ch;

	/* Enable UART0 and UART1 and delay a few clocks */
	LM4_SYSTEM_RCGCUART |= 0x03;
	scratch = LM4_SYSTEM_RCGCUART;

	/* Configure GPIOs */
	configure_gpio();

	/* Configure UART0 and UART1 (identically) */
	for (ch = 0; ch < 2; ch++) {
		/* Disable the port */
		LM4_UART_CTL(ch) = 0x0300;
		/* Set the baud rate divisor */
		LM4_UART_IBRD(ch) = (CPU_CLOCK / 16) / BAUD_RATE;
		LM4_UART_FBRD(ch) =
			(((CPU_CLOCK / 16) % BAUD_RATE) * 64 + BAUD_RATE / 2) /
			BAUD_RATE;
		/* 8-N-1, FIFO enabled.  Must be done after setting
		 * the divisor for the new divisor to take effect. */
		LM4_UART_LCRH(ch) = 0x70;
		/* Interrupt when RX fifo at minimum (>= 1/8 full), and TX fifo
		 * when <= 1/4 full */
		LM4_UART_IFLS(ch) = 0x01;
		/* Unmask receive-FIFO, receive-timeout.  We need
		 * receive-timeout because the minimum RX FIFO depth is 1/8 = 2
		 * bytes; without the receive-timeout we'd never be notified
		 * about single received characters. */
		LM4_UART_IM(ch) = 0x50;
		/* Enable the port */
		LM4_UART_CTL(ch) |= 0x0001;
	}

	/* Print hello on UART1 for debugging */
	/* TODO: remove in production */
	{
		const char *c = "Hello on UART1\r\n";
		while (*c)
			uart_comx_putc(*c++);
	}

	return EC_SUCCESS;
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

	if (was_empty) {
		/* Re-enable the transmit interrupt, then forcibly trigger the
		 * interrupt.  This works around a hardware problem with the
		 * UART where the FIFO only triggers the interrupt when its
		 * threshold is _crossed_, not just met. */
		LM4_UART_IM(0) |= 0x20;
		task_trigger_irq(LM4_IRQ_UART0);
	}

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

	if (was_empty) {
		/* Re-enable the transmit interrupt, then forcibly trigger the
		 * interrupt.  This works around a hardware problem with the
		 * UART where the FIFO only triggers the interrupt when its
		 * threshold is _crossed_, not just met. */
		LM4_UART_IM(0) |= 0x20;
		task_trigger_irq(LM4_IRQ_UART0);
	}

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
		if (!(LM4_UART_IM(0) & 0x20)) {
			LM4_UART_IM(0) |= 0x20;
			task_trigger_irq(LM4_IRQ_UART0);
		}
	}

	/* Wait for transmit FIFO empty */
	while (!(LM4_UART_FR(0) & 0x80)) {}
}

void uart_emergency_flush(void)
{
	do {
		/* Copy output from buffer until TX fifo full
		 * or output buffer empty
		 */
		while (!(LM4_UART_FR(0) & 0x20) &&
		        (tx_buf_head != tx_buf_tail)) {
			LM4_UART_DR(0) = tx_buf[tx_buf_tail];
			tx_buf_tail = TX_BUF_NEXT(tx_buf_tail);
		}
		/* Wait for transmit FIFO empty */
		while (!(LM4_UART_FR(0) & 0x80)) {}
	} while (tx_buf_head != tx_buf_tail);
}


void uart_flush_input(void)
{
	/* Disable interrupts */
	task_disable_irq(LM4_IRQ_UART0);

	/* Call interrupt handler to empty the hardware FIFO */
	uart_0_interrupt();

	/* Clear the input buffer */
	rx_buf_tail = rx_buf_head;

	/* Re-enable interrupts */
	task_enable_irq(LM4_IRQ_UART0);
}


int uart_peek(int c)
{
	int index = -1;
	int i = 0;

	/* Disable interrupts while we pull characters out, because the
	 * interrupt handler can also modify the tail pointer. */
	task_disable_irq(LM4_IRQ_UART0);

	/* Call interrupt handler to empty the hardware FIFO.  The minimum
	 * FIFO trigger depth is 1/8 (2 chars), so this is the only way to
	 * ensure we've pulled the very last character out of the FIFO. */
	uart_0_interrupt();

	for (i = rx_buf_tail; i != rx_buf_head; i = RX_BUF_NEXT(i)) {
		if (rx_buf[i] == c) {
			index = (RX_BUF_SIZE + i - rx_buf_tail) &
					(RX_BUF_SIZE - 1);
			break;
		}
	}

	/* Re-enable interrupts */
	task_enable_irq(LM4_IRQ_UART0);

	return index;
}


int uart_getc(void)
{
	int c;

	/* Disable interrupts */
	task_disable_irq(LM4_IRQ_UART0);

	/* Call interrupt handler to empty the hardware FIFO */
	uart_0_interrupt();

	if (rx_buf_tail == rx_buf_head) {
		c = -1;  /* No pending input */
	} else {
		c = rx_buf[rx_buf_tail];
		rx_buf_tail = RX_BUF_NEXT(rx_buf_tail);
	}

	/* Re-enable interrupts */
	task_enable_irq(LM4_IRQ_UART0);

	return c;
}


int uart_gets(char *dest, int size)
{
	int got = 0;
	int c;

	/* Disable interrupts while we pull characters out, because the
	 * interrupt handler can also modify the tail pointer. */
	task_disable_irq(LM4_IRQ_UART0);

	/* Call interrupt handler to empty the hardware FIFO */
	uart_0_interrupt();

	/* Read characters */
	while (got < size - 1 && rx_buf_tail != rx_buf_head) {
		c = rx_buf[rx_buf_tail];
		dest[got++] = c;
		rx_buf_tail = RX_BUF_NEXT(rx_buf_tail);
		if (c == '\n')
			break;  /* Stop on newline */
	}

	/* Re-enable interrupts */
	task_enable_irq(LM4_IRQ_UART0);

	/* Null-terminate */
	dest[got] = '\0';

	/* Return the length we got */
	return got;
}


/*****************************************************************************/
/* COMx functions */


int uart_comx_putc_ok(void)
{
	if (LM4_UART_FR(1) & 0x20) {
		/* FIFO is full, so enable transmit interrupt to let us know
		 * when it empties. */
		LM4_UART_IM(1) |= 0x20;
		return 0;
	} else {
		return 1;
	}
}


void uart_comx_putc(int c)
{
	LM4_UART_DR(1) = c;
}
