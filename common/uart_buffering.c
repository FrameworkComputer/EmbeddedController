/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code to do UART buffering and printing */

#include <stdarg.h>

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "printf.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/*
 * Transmit and receive buffer sizes must be power of 2 for the macros below
 * to work properly.
 */
#ifndef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 512
#endif

#ifndef CONFIG_UART_RX_BUF_SIZE
/* Must be larger than CONSOLE_INPUT_LINE_SIZE to copy and paste scripts */
#define CONFIG_UART_RX_BUF_SIZE 128
#endif

#define HISTORY_SIZE 8

/* Macros to advance in the circular buffers */
#define TX_BUF_NEXT(i) (((i) + 1) & (CONFIG_UART_TX_BUF_SIZE - 1))
#define RX_BUF_NEXT(i) (((i) + 1) & (CONFIG_UART_RX_BUF_SIZE - 1))
#define RX_BUF_PREV(i) (((i) - 1) & (CONFIG_UART_RX_BUF_SIZE - 1))
#define CMD_HIST_NEXT(i) (((i) + 1) & (HISTORY_SIZE - 1))
#define CMD_HIST_PREV(i) (((i) - 1) & (HISTORY_SIZE - 1))

/* Macros to calculate difference of pointers in the circular buffers. */
#define TX_BUF_DIFF(i, j) (((i) - (j)) & (CONFIG_UART_TX_BUF_SIZE - 1))
#define RX_BUF_DIFF(i, j) (((i) - (j)) & (CONFIG_UART_RX_BUF_SIZE - 1))

/* ASCII control character; for example, CTRL('C') = ^C */
#define CTRL(c) ((c) - '@')

/* Transmit and receive buffers */
static volatile char tx_buf[CONFIG_UART_TX_BUF_SIZE];
static volatile int tx_buf_head;
static volatile int tx_buf_tail;
static volatile char rx_buf[CONFIG_UART_RX_BUF_SIZE];
static volatile int rx_buf_head;
static volatile int rx_buf_tail;
static volatile char rx_cur_buf[CONSOLE_INPUT_LINE_SIZE];
static volatile int rx_cur_buf_tail;
static volatile int rx_cur_buf_head;
static volatile int rx_cur_buf_ptr;
static int last_rx_was_cr;
static int tx_snapshot_head;
static int tx_snapshot_tail;
static int uart_suspended;

static enum {
	ESC_OUTSIDE,   /* Not in escape code */
	ESC_START,     /* Got ESC */
	ESC_BAD,       /* Bad escape sequence */
	ESC_BRACKET,   /* Got ESC [ */
	ESC_BRACKET_1, /* Got ESC [ 1 */
	ESC_BRACKET_3, /* Got ESC [ 3 */
	ESC_O,         /* Got ESC O */
} esc_state;

/* Command history */
struct cmd_history_t {
	volatile int head;
	volatile int tail;
};
static struct cmd_history_t cmd_history[HISTORY_SIZE];
static volatile int cmd_history_head;
static volatile int cmd_history_tail;
static volatile int cmd_history_ptr;

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
	int tx_buf_next;

	/* Do newline to CRLF translation */
	if (c == '\n' && __tx_char(NULL, '\r'))
		return 1;

	tx_buf_next = TX_BUF_NEXT(tx_buf_head);
	if (tx_buf_next == tx_buf_tail)
		return 1;

	tx_buf[tx_buf_head] = c;
	tx_buf_head = tx_buf_next;
	return 0;
}

/**
 * Write a number directly to the UART.
 *
 * @param val number to write; must be >1.
 */
static void uart_write_int(int val)
{
	if (val <= 0)
		return;

	if (val > 9)
		uart_write_int(val / 10);

	uart_write_char((val % 10) + '0');
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

static void move_rx_ptr_end(void)
{
	if (rx_cur_buf_ptr == rx_cur_buf_head)
		return;

	uart_write_char(0x1B);
	uart_write_char('[');
	uart_write_int(rx_cur_buf_head - rx_cur_buf_ptr);
	uart_write_char('C');

	rx_cur_buf_ptr = rx_cur_buf_head;
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

static void move_rx_ptr_begin(void)
{
	if (rx_cur_buf_ptr == 0)
		return;

	uart_write_char(0x1B);
	uart_write_char('[');
	uart_write_int(rx_cur_buf_ptr);
	uart_write_char('D');

	rx_cur_buf_ptr = 0;
}

static void repeat_char(char c, int cnt)
{
	while (cnt--)
		uart_write_char(c);
}

static void handle_backspace(void)
{
	int ptr;

	if (!rx_cur_buf_ptr)
		return;  /* Already at beginning of line */

	/* Move cursor back */
	uart_write_char('\b');

	/* Move text after cursor and also update rx buffer. */
	for (ptr = rx_cur_buf_ptr; ptr < rx_cur_buf_head; ++ptr) {
		uart_write_char(rx_cur_buf[ptr]);
		rx_cur_buf[ptr - 1] = rx_cur_buf[ptr];
	}

	/* Space over last character and move cursor to correct position */
	uart_write_char(' ');
	repeat_char('\b', ptr - rx_cur_buf_ptr + 1);

	--rx_cur_buf_head;
	--rx_cur_buf_ptr;
}

static void handle_kill(void)
{
	if (rx_cur_buf_ptr == rx_cur_buf_head)
		return;

	/* Space over all following characters */
	repeat_char(' ', rx_cur_buf_head - rx_cur_buf_ptr);
	repeat_char('\b', rx_cur_buf_head - rx_cur_buf_ptr);

	rx_cur_buf_head = rx_cur_buf_ptr;
}

static void reprint_current(void)
{
	int ptr;

	uart_write_char(CTRL('L'));
	uart_write_char('>');
	uart_write_char(' ');

	for (ptr = 0; ptr < rx_cur_buf_head; ptr++)
			uart_write_char(rx_cur_buf[ptr]);

	repeat_char('\b', ptr - rx_cur_buf_ptr);
}

static void insert_char(char c)
{
	int ptr;

	/* On overflow, discard input */
	if (rx_cur_buf_head == CONSOLE_INPUT_LINE_SIZE && c != '\n')
		return;

	/* Move buffer ptr to the end if 'c' is new line */
	if (c == '\n')
		rx_cur_buf_ptr = rx_cur_buf_head;

	/* Move text after cursor. */
	for (ptr = rx_cur_buf_ptr; ptr < rx_cur_buf_head; ++ptr)
		uart_write_char(rx_cur_buf[ptr]);

	/* Insert character and move cursor to correct position */
	repeat_char('\b', ptr - rx_cur_buf_ptr);
	for (ptr = rx_cur_buf_head; ptr > rx_cur_buf_ptr; --ptr)
		rx_cur_buf[ptr] = rx_cur_buf[ptr - 1];
	rx_cur_buf[rx_cur_buf_ptr] = c;
	++rx_cur_buf_head;
	++rx_cur_buf_ptr;
}

static int rx_buf_space_available(void)
{
	if (cmd_history_head == cmd_history_tail)
		return CONFIG_UART_RX_BUF_SIZE;
	return RX_BUF_DIFF(cmd_history[cmd_history_tail].tail,
			   cmd_history[CMD_HIST_PREV(cmd_history_head)].head);
}

static void history_save(void)
{
	int ptr;
	int tail, head;
	int hist_id;

	/* If not enough space in rx buffer, discard the oldest history */
	while (rx_buf_space_available() < rx_cur_buf_head)
		cmd_history_tail = CMD_HIST_NEXT(cmd_history_tail);

	/* If history buffer is full, discard the oldest one */
	hist_id = cmd_history_head;
	cmd_history_head = CMD_HIST_NEXT(cmd_history_head);
	if (cmd_history_head == cmd_history_tail)
		cmd_history_tail = CMD_HIST_NEXT(cmd_history_tail);

	/* Copy the current command, but do not save the '\n' */
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

	/*
	 * Stash the current command if we are not currently using history.
	 * Prevent loading history if there is no space to stash current
	 * command.
	 */
	if (cmd_history_ptr == cmd_history_head) {
		int last_id = CMD_HIST_PREV(cmd_history_head);
		int last_len = RX_BUF_DIFF(cmd_history[last_id].head,
					   cmd_history[last_id].tail);
		if (last_len + rx_cur_buf_head > CONFIG_UART_RX_BUF_SIZE)
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

/**
 * Escape code handler
 *
 * @param c             Next received character.
 */
static void handle_esc(int c)
{
	switch (esc_state) {
	case ESC_START:
		if (c == '[') {
			esc_state = ESC_BRACKET;
			return;
		} else if (c == 'O') {
			esc_state = ESC_O;
			return;
		}
		break;

	case ESC_BRACKET:
		if (c == '1') {
			esc_state = ESC_BRACKET_1;
			return;
		} else if (c == '3') {
			esc_state = ESC_BRACKET_3;
			return;
		}

		if (c == 'A') /* Up key */
			history_prev();
		else if (c == 'B') /* Down key */
			history_next();
		else if (c == 'C') /* Right key */
			move_rx_ptr_fwd();
		else if (c == 'D') /* Left key */
			move_rx_ptr_bwd();
		break;

	case ESC_O:
		if (c == 'F') /* End key */
			move_rx_ptr_end();
		break;

	case ESC_BRACKET_1:
		if (c == '~') /* Home key */
			move_rx_ptr_begin();
		break;

	case ESC_BRACKET_3:
		if (c == '~') { /* Del key */
			if (rx_cur_buf_ptr != rx_cur_buf_head) {
				move_rx_ptr_fwd();
				handle_backspace();
			}
		}
		break;

	default:
		break;
	}

	/* Check if the escape code is done */
	if (isalpha(c) || c == '~')
		esc_state = ESC_OUTSIDE;
	else
		esc_state = ESC_BAD;
}

/**
 * Handle next character of console input.
 */
static void handle_console_char(int c)
{
	/* Translate CR and CRLF to LF (newline) */
	if (c == '\r') {
		last_rx_was_cr = 1;
		c = '\n';
	} else if (c == '\n' && last_rx_was_cr) {
		last_rx_was_cr = 0;
		return;
	} else {
		last_rx_was_cr = 0;
	}

	/* Handle terminal escape sequences (ESC [ ...) */
	if (c == 0x1B) {
		esc_state = ESC_START;
		return;
	} else if (esc_state) {
		handle_esc(c);
		return;
	}

	/* Handle control characters */
	if (c == '\b' || c == 0x7f) {
		handle_backspace();
	} else if (c == CTRL('A')) {
		move_rx_ptr_begin();
	} else if (c == CTRL('E')) {
		move_rx_ptr_end();
	} else if (c == CTRL('L')) {
		reprint_current();
	} else if (c == CTRL('K')) {
		handle_kill();
	} else if (c == CTRL('N')) {
		history_next();
	} else if (c == CTRL('P')) {
		history_prev();
	} else if (c == CTRL('Q')) {
		uart_suspended = 1;
		uart_tx_stop();
	} else if (c == CTRL('S')) {
		uart_suspended = 0;
		if (uart_tx_stopped())
			uart_tx_start();
	} else if (c == '\n') {  /* Newline */
		uart_write_char('\r');
		uart_write_char('\n');
		insert_char(c);
		console_has_input();
	} else if (isprint(c)) {
		/*
		 * Normal printable character.  Echo directly to the transmit
		 * FIFO so we don't interfere with the transmit buffer.
		 */
		uart_write_char(c);
		insert_char(c);
	}
}

/**
 * Copy output from buffer until TX fifo full or output buffer empty.
 *
 * May be called from interrupt context.
 */
static void fill_tx_fifo(void)
{
	while (uart_tx_ready() && (tx_buf_head != tx_buf_tail)) {
		uart_write_char(tx_buf[tx_buf_tail]);
		tx_buf_tail = TX_BUF_NEXT(tx_buf_tail);
	}
}

/**
 * Helper for UART processing.
 */
void uart_process(void)
{
	/* Copy input from buffer until RX fifo empty */
	while (uart_rx_available())
		handle_console_char(uart_read_char());

	if (uart_suspended)
		return;

	/* Copy output from buffer until TX fifo full or output buffer empty */
	fill_tx_fifo();

	/* If output buffer is empty, disable transmit interrupt */
	if (tx_buf_tail == tx_buf_head)
		uart_tx_stop();
}

int uart_puts(const char *outstr)
{
	/* Put all characters in the output buffer */
	while (*outstr) {
		if (__tx_char(NULL, *outstr++) != 0)
			break;
	}

	if (!uart_suspended && uart_tx_stopped())
		uart_tx_start();

	/* Successful if we consumed all output */
	return *outstr ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

int uart_vprintf(const char *format, va_list args)
{
	int rv = vfnprintf(__tx_char, NULL, format, args);

	if (!uart_suspended && uart_tx_stopped())
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

	/*
	 * If we're in interrupt context, copy output explicitly, since the
	 * UART interrupt may not be able to preempt this one.
	 */
	if (in_interrupt_context()) {
		do {
			/* Copy until TX fifo full or output buffer empty */
			fill_tx_fifo();

			/* Wait for transmit FIFO empty */
			uart_tx_flush();
		} while (tx_buf_head != tx_buf_tail);
		return;
	}

	/* Wait for buffer to empty */
	while (tx_buf_head != tx_buf_tail) {
		/*
		 * It's possible we're in some other interrupt, and the
		 * previous context was doing a printf() or puts() but hadn't
		 * enabled the UART interrupt.  Check if the interrupt is
		 * disabled, and if so, re-enable and trigger it.  Note that
		 * this check is inside the while loop, so we'll be safe even
		 * if the context switches away from us to another partial
		 * printf() and back.
		 */
		if (uart_tx_stopped())
			uart_tx_start();
	}

	/* Wait for transmit FIFO empty */
	uart_tx_flush();
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

	/*
	 * Disable interrupts while we pull characters out, because the
	 * interrupt handler can also modify the tail pointer.
	 */
	uart_disable_interrupt();

	/*
	 * Call interrupt handler to empty the hardware FIFO.  The minimum
	 * FIFO trigger depth is 1/8 (2 chars), so this is the only way to
	 * ensure we've pulled the very last character out of the FIFO.
	 */
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

	/*
	 * Disable interrupts while we pull characters out, because the
	 * interrupt handler can also modify the tail pointer.
	 */
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

/*****************************************************************************/
/* Host commands */

static int host_command_console_snapshot(struct host_cmd_handler_args *args)
{
	/*
	 * Only allowed on unlocked system, since console output contains
	 * keystroke data.
	 */
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	/* Assume the whole circular buffer is full */
	tx_snapshot_head = tx_buf_head;
	tx_snapshot_tail = TX_BUF_NEXT(tx_snapshot_head);

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

static int host_command_console_read(struct host_cmd_handler_args *args)
{
	char *dest = (char *)args->response;

	/*
	 * Only allowed on unlocked system, since console output contains
	 * keystroke data.
	 */
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	/* If no snapshot data, return empty response */
	if (tx_snapshot_head == tx_snapshot_tail)
		return EC_RES_SUCCESS;

	/* Copy data to response */
	while (tx_snapshot_tail != tx_snapshot_head &&
	       args->response_size < args->response_max - 1) {

		/*
		 * Copy only non-zero bytes, so that we don't copy unused
		 * bytes if the buffer hasn't completely rolled at boot.
		 */
		if (tx_buf[tx_snapshot_tail]) {
			*(dest++) = tx_buf[tx_snapshot_tail];
			args->response_size++;
		}

		tx_snapshot_tail = TX_BUF_NEXT(tx_snapshot_tail);
	}

	/* Null-terminate */
	*(dest++) = '\0';
	args->response_size++;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CONSOLE_READ,
		     host_command_console_read,
		     EC_VER_MASK(0));
