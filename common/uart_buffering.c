/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code to do UART buffering and printing */

#include <stdarg.h>

#include "console.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Buffer sizes; should be power of 2 */
#define TX_BUF_SIZE 512
#define RX_BUF_SIZE 128  /* suggest larger than 80 to copy&paste script. */
#define HISTORY_SIZE 8

/* The size limit of single command */
#define RX_LINE_SIZE 80

/* Macros to advance in the circular buffers */
#define TX_BUF_NEXT(i) (((i) + 1) & (TX_BUF_SIZE - 1))
#define RX_BUF_NEXT(i) (((i) + 1) & (RX_BUF_SIZE - 1))
#define RX_BUF_PREV(i) (((i) - 1) & (RX_BUF_SIZE - 1))
#define CMD_HIST_NEXT(i) (((i) + 1) & (HISTORY_SIZE - 1))
#define CMD_HIST_PREV(i) (((i) - 1) & (HISTORY_SIZE - 1))

/* Macro to calculate difference of pointers in the circular receive buffer. */
#define RX_BUF_DIFF(i, j) (((i) - (j)) & (RX_BUF_SIZE - 1))

/* Transmit and receive buffers */
static volatile char tx_buf[TX_BUF_SIZE];
static volatile int tx_buf_head;
static volatile int tx_buf_tail;
static volatile char rx_buf[RX_BUF_SIZE];
static volatile int rx_buf_head;
static volatile int rx_buf_tail;
static volatile char rx_cur_buf[RX_LINE_SIZE];
static volatile int rx_cur_buf_tail;
static volatile int rx_cur_buf_head;
static volatile int rx_cur_buf_ptr;
static int last_rx_was_cr;
static int in_escape;
static char esc_seq_char;

/* Command history */
struct cmd_history_t {
	volatile int head;
	volatile int tail;
};
static struct cmd_history_t cmd_history[HISTORY_SIZE];
static volatile int cmd_history_head;
static volatile int cmd_history_tail;
static volatile int cmd_history_ptr;

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

static void move_rx_ptr_fwd(void)
{
	if (rx_cur_buf_ptr != rx_cur_buf_head) {
		++rx_cur_buf_ptr;
		uart_write_char(0x1B);
		uart_write_char('[');
		uart_write_char('1');
		uart_write_char('C');
	}
}

static void move_rx_ptr_bwd(void)
{
	if (rx_cur_buf_ptr != 0) {
		--rx_cur_buf_ptr;
		uart_write_char(0x1B);
		uart_write_char('[');
		uart_write_char('1');
		uart_write_char('D');
	}
}

static void repeat_char(char c, int cnt)
{
	while (cnt--)
		uart_write_char(c);
}

static void handle_backspace(void)
{
	if (rx_cur_buf_ptr != 0) {
		/* Move texts after cursor and also update rx buffer. */
		int ptr;
		for (ptr = rx_cur_buf_ptr; ptr < rx_cur_buf_head; ++ptr) {
			uart_write_char(rx_cur_buf[ptr]);
			rx_cur_buf[ptr - 1] = rx_cur_buf[ptr];
		}

		/* Space over last character and move cursor back to correct
		 * position.
		 */
		uart_write_char(' ');
		repeat_char('\b', ptr - rx_cur_buf_ptr + 1);

		--rx_cur_buf_head;
		--rx_cur_buf_ptr;
	}
	else
		/* Cursor moves pass the first character. Move it back. */
		uart_write_char(' ');
}

static void insert_char(char c)
{
	int ptr;

	/* On overflow, discard input */
	if (rx_cur_buf_head == RX_LINE_SIZE)
		return;

	/* Move buffer ptr to the end if 'c' is new line */
	if (c == '\n')
		rx_cur_buf_ptr = rx_cur_buf_head;

	/* Move text after cursor. */
	for (ptr = rx_cur_buf_ptr; ptr < rx_cur_buf_head; ++ptr)
		uart_write_char(rx_cur_buf[ptr]);

	/* Insert character to rx buffer and move cursor to correct
	 * position.
	 */
	repeat_char('\b', ptr - rx_cur_buf_ptr);
	for (ptr = rx_cur_buf_head; ptr > rx_cur_buf_ptr; --ptr)
		rx_cur_buf[ptr] = rx_cur_buf[ptr - 1];
	rx_cur_buf[rx_cur_buf_ptr] = c;
	++rx_cur_buf_head;
	++rx_cur_buf_ptr;

	/* Insert character directly into rx_buf if not in console mode. */
	if (!console_mode) {
		rx_buf[rx_buf_head] = c;
		rx_buf_head = RX_BUF_NEXT(rx_buf_head);
		if (rx_buf_tail == rx_buf_head)
			rx_buf_tail = RX_BUF_NEXT(rx_buf_tail);
	}
}

static int rx_buf_space_available(void)
{
	if (cmd_history_head == cmd_history_tail)
		return RX_BUF_SIZE;
	return RX_BUF_DIFF(cmd_history[cmd_history_tail].tail,
			   cmd_history[CMD_HIST_PREV(cmd_history_head)].head);
}

static void history_save(void)
{
	int ptr;
	int tail, head;
	int hist_id;

	/* If there is not enough space in rx buffer, discard the oldest
	 * history. */
	while (rx_buf_space_available() < rx_cur_buf_head)
		cmd_history_tail = CMD_HIST_NEXT(cmd_history_tail);

	/* If history buffer is full, discard the oldest one */
	hist_id = cmd_history_head;
	cmd_history_head = CMD_HIST_NEXT(cmd_history_head);
	if (cmd_history_head == cmd_history_tail)
		cmd_history_tail = CMD_HIST_NEXT(cmd_history_tail);

	/* Copy the current command, but we do not save the '\n' */
	if (hist_id == cmd_history_tail)
		tail = 0;
	else
		tail = RX_BUF_NEXT(cmd_history[CMD_HIST_PREV(hist_id)].head);
	head = tail;
	for (ptr = 0; ptr < rx_cur_buf_head; ++ptr, head = RX_BUF_NEXT(head))
		rx_buf[head] = rx_cur_buf[ptr];
	if (rx_buf[RX_BUF_PREV(head)] == '\n') {
		head = RX_BUF_PREV(head);
		rx_buf[head] = '\0';
	}

	cmd_history[hist_id].head = head;
	cmd_history[hist_id].tail = tail;
}

static void history_load(int id)
{
	int head = cmd_history[id].head;
	int tail = cmd_history[id].tail;
	int ptr;

	cmd_history_ptr = id;

	/* Move cursor back to begin of the line. */
	repeat_char('\b', rx_cur_buf_ptr);

	/* Load command and print it. */
	for (ptr = tail, rx_cur_buf_ptr = 0; ptr != head;
			ptr = RX_BUF_NEXT(ptr), ++rx_cur_buf_ptr) {
		rx_cur_buf[rx_cur_buf_ptr] = rx_buf[ptr];
		uart_write_char(rx_buf[ptr]);
	}

	/* If needed, space over the remaining text. */
	if (rx_cur_buf_ptr < rx_cur_buf_head) {
		repeat_char(' ', rx_cur_buf_head - rx_cur_buf_ptr);
		repeat_char('\b', rx_cur_buf_head - rx_cur_buf_ptr);
	}

	rx_cur_buf_head = rx_cur_buf_ptr;
}

static void history_prev(void)
{
	if (cmd_history_ptr == cmd_history_tail)
		return;

	/* Stash the current command if we are not currently using history.
	 * Prevent loading history if there is no space to stash current
	 * command. */
	if (cmd_history_ptr == cmd_history_head) {
		int last_id = CMD_HIST_PREV(cmd_history_head);
		int last_len = RX_BUF_DIFF(cmd_history[last_id].head,
					   cmd_history[last_id].tail);
		if (last_len + rx_cur_buf_head > RX_BUF_SIZE)
			return;

		history_save();
	}

	cmd_history_ptr = CMD_HIST_PREV(cmd_history_ptr);
	history_load(cmd_history_ptr);
}

static void history_next(void)
{
	if (cmd_history_ptr == cmd_history_head)
		return;

	cmd_history_ptr = CMD_HIST_NEXT(cmd_history_ptr);
	history_load(cmd_history_ptr);

	/* Remove the stashed command if we just loaded it. */
	if (cmd_history_ptr == CMD_HIST_PREV(cmd_history_head))
		cmd_history_head = cmd_history_ptr;
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

			/* Handle left and right key, and eat other terminal
			 * escape sequences (ESC [ ...).
			 * Would be really cool if we used arrow keys to edit
			 * command history, but for now it's sufficient just to
			 * keep them from causing problems. */
			if (c == 0x1B) {
				in_escape = 1;
				esc_seq_char = c;
				continue;
			} else if (in_escape) {
				if (esc_seq_char == 0x1B && c == '[')
					esc_seq_char = '[';
				else if (esc_seq_char == '[') {
					if (c == 'A') /* Up key */
						history_prev();
					else if (c == 'B') /* Down key */
						history_next();
					else if (c == 'C') /* Right key */
						move_rx_ptr_fwd();
					else if (c == 'D') /* Left key */
						move_rx_ptr_bwd();
					esc_seq_char = 0;
				}
				else
					esc_seq_char = 0;

				if (isalpha(c) || c == '~') {
					esc_seq_char = 0;
					in_escape = 0;
				}
				continue;
			}

			/* Echo characters directly to the transmit FIFO so we
			 * don't interfere with the transmit buffer. */
			if (c == '\n')
				uart_write_char('\r');
			uart_write_char(c);

			/* Handle backspace if we can */
			if (c == '\b') {
				handle_backspace();
				continue;
			}
		}

		insert_char(c);

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

	if (!enable)
		rx_cur_buf_ptr = rx_cur_buf_head;
}


int uart_puts(const char *outstr)
{
	/* Put all characters in the output buffer */
	while (*outstr) {
		if (__tx_char(*outstr++) != 0)
			break;
	}

	if (uart_tx_stopped())
		uart_tx_start();

	/* Successful if we consumed all output */
	return *outstr ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}


int uart_vprintf(const char *format, va_list args)
{
	static const char int_chars[] = "0123456789abcdef";
	static const char error_str[] = "ERROR";
	char intbuf[34];
		/* Longest uint64 in decimal = 20
		 * longest uint32 in binary  = 32
		 */
	int dropped_chars = 0;
	int is_left;
	int pad_zero;
	int pad_width;
	char *vstr;
	int vlen;

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
			uint64_t v;
			int is_negative = 0;
			int is_64bit = 0;
			int base = 10;
			int fixed_point = 0;

			/* Handle fixed point numbers */
			if (c == '.') {
				c = *format++;
				if (c < '0' || c > '9') {
					format = error_str;
					continue;
				}
				fixed_point = c - '0';
				c = *format++;
			}

			if (c == 'l') {
				is_64bit = 1;
				c = *format++;
			}

			/* Special-case: %T = current time */
			if (c == 'T') {
				v = get_time().val;
				is_64bit = 1;
				fixed_point = 6;
			} else if (is_64bit) {
				v = va_arg(args, uint64_t);
			} else {
				v = va_arg(args, uint32_t);
			}

			switch (c) {
			case 'd':
				if (is_64bit) {
					if ((int64_t)v < 0) {
						is_negative = 1;
						if (v != (1ULL << 63))
							v = -v;
					}
				} else {
					if ((int)v < 0) {
						is_negative = 1;
						if (v != (1ULL << 31))
							v = -(int)v;
					}
				}
				break;
			case 'u':
			case 'T':
				break;
			case 'x':
			case 'p':
				base = 16;
				break;
			case 'b':
				base = 2;
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

			/* Handle digits to right of decimal for fixed point
			 * numbers. */
			for (vlen = 0; vlen < fixed_point; vlen++)
				*(--vstr) = int_chars[uint64divmod(&v, 10)];
			if (fixed_point)
				*(--vstr) = '.';

			if (!v)
				*(--vstr) = '0';

			while (v)
				*(--vstr) = int_chars[uint64divmod(&v, base)];

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

	if (uart_tx_stopped())
		uart_tx_start();

	/* Successful if we consumed all output */
	return dropped_chars ? EC_ERROR_OVERFLOW : EC_SUCCESS;
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
/* For use when debugging verified boot. We could wrap it with a real function,
 * but it's rarely needed and this doesn't add any extra code. We have to
 * declare it here in order for this trick to work.  */
void VbExDebug(const char *format, ...)
	__attribute__((weak, alias("uart_printf")));


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
	rx_cur_buf_head = 0;
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

	for (i = 0; i < rx_cur_buf_head; ++i) {
		if (rx_cur_buf[i] == c) {
			index = i;
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

	/* Remove the stashed command if any. */
	if (cmd_history_ptr != cmd_history_head)
		cmd_history_head = CMD_HIST_PREV(cmd_history_head);

	/* Record last command. */
	if (!(rx_cur_buf_head == 1 && rx_cur_buf[0] == '\n'))
		history_save();
	cmd_history_ptr = cmd_history_head;

	/* Read characters */
	while (got < size - 1 && got < rx_cur_buf_head) {
		c = rx_cur_buf[got];
		dest[got++] = c;
		if (c == '\n')
			break;  /* Stop on newline */
	}
	rx_cur_buf_ptr = 0;
	rx_cur_buf_head = 0;
	rx_cur_buf_tail = rx_cur_buf_head;

	/* Re-enable interrupts */
	uart_enable_interrupt();

	/* Null-terminate */
	dest[got] = '\0';

	/* Return the length we got */
	return got;
}
