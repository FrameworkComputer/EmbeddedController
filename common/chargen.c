/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "console.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "usb_console.h"
#include "util.h"
#include "watchdog.h"

#ifndef SECTION_IS_RO

/**
 * Some unit tests do not have a watchdog enabled and the watchdog
 * functions are stubbed. Define a default watchdog period in this case.
 */
#ifdef CONFIG_WATCHDOG_PERIOD_MS
#define CHARGEN_WATCHDOG_PERIOD_MS CONFIG_WATCHDOG_PERIOD_MS
#else
#define CHARGEN_WATCHDOG_PERIOD_MS 1600
#endif

/*
 * Microseconds time to drain entire UART_TX console buffer at 115200 b/s, 10
 * bits per character.
 */
#define BUFFER_DRAIN_TIME_US \
	(1000000UL * 10 * CONFIG_UART_TX_BUF_SIZE / CONFIG_UART_BAUD_RATE)

struct deferred_chargen_ctx {
	int wrap_value;
	uint32_t seq_number;
	int (*putc)(int c);
	int (*tx_is_blocked)(void);
};
static struct deferred_chargen_ctx chargen_ctx;

static void acquire_console(void)
{
#if !defined(CONFIG_USB_CONSOLE) && !defined(CONFIG_USB_CONSOLE_STREAM)
	uart_shell_rx_bypass(true);
#endif
#if !defined(CONFIG_ZEPHYR) && !defined(BOARD_HOST)
	/* The legacy fw console does not have an rx bypass feature (it is
	 * stubbed out).  Disable the console task so that it does not
	 * steal character reads from chargen.
	 */
	if (task_start_called())
		task_disable_task(TASK_ID_CONSOLE);
#endif /* !CONFIG_ZEPHYR  && !BOARD_HOST */
}

static void release_console(void)
{
#if !defined(CONFIG_USB_CONSOLE) && !defined(CONFIG_USB_CONSOLE_STREAM)
	uart_shell_rx_bypass(false);
#endif
#if !defined(CONFIG_ZEPHYR) && !defined(BOARD_HOST)
	if (task_start_called())
		task_enable_task(TASK_ID_CONSOLE);
#endif /* !CONFIG_ZEPHYR  && !BOARD_HOST */
}

static void run_chargen(void)
{
	int wrap_value = chargen_ctx.wrap_value;
	uint32_t seq_number = chargen_ctx.seq_number;
	int (*putc_)(int c) = chargen_ctx.putc;
	int (*tx_is_blocked_)(void) = chargen_ctx.tx_is_blocked;

	timestamp_t prev_watchdog_time;
	uint8_t c = '0';
	uint32_t seq_counter = 0;
	int wrap_counter = 0;

	acquire_console();

	while (uart_getc() != -1 || usb_getc() != -1)
		; /* Drain received characters, if any. */

#ifdef CONFIG_ZEPHYR
	k_sched_lock();
#endif
	prev_watchdog_time = get_time();
	while (uart_getc() != 'x' && usb_getc() != 'x') {
		timestamp_t current_time;

		while (tx_is_blocked_()) {
			/*
			 * Let's let other tasks run for a bit while buffer is
			 * being drained a little.
			 */
			usleep(BUFFER_DRAIN_TIME_US / 10);

			current_time = get_time();

			if ((current_time.val - prev_watchdog_time.val) <
			    (CHARGEN_WATCHDOG_PERIOD_MS * 1000 / 2))
				continue;

			watchdog_reload();
			prev_watchdog_time.val = current_time.val;
		}

		if (IS_ENABLED(CONFIG_ZEPHYR) && c == '0')
			watchdog_reload();

		putc_(c++);

		if (seq_number && (++seq_counter == seq_number))
			break;

		if (wrap_value && (++wrap_counter == wrap_value)) {
			c = '0';
			wrap_counter = 0;
			continue;
		}

		if (c == ('z' + 1))
			c = '0';
		else if (c == ('Z' + 1))
			c = 'a';
		else if (c == ('9' + 1))
			c = 'A';
	}
#ifdef CONFIG_ZEPHYR
	k_sched_unlock();
#endif

	putc_('\n');

	release_console();
}
DECLARE_DEFERRED(run_chargen);

/*
 * Generate a stream of characters on the UART (and USB) console.
 *
 * The stream is an ever incrementing pattern of characters from the following
 * set: 0..9A..Za..z.
 *
 * The two optional integer command line arguments work as follows:
 *
 * argv[1] - reset the pattern after this many characters have been printed.
 *           Setting this value to the width of the terminal window results
 *           in a very regular stream showing on the terminal, where it is
 *           easy to observe disruptions.
 * argv[2] - limit number of printed characters to this amount. If not
 *           specified - keep printing indefinitely.
 *
 * Hitting 'x' on the keyboard stops the generator.
 */
static int command_chargen(int argc, const char **argv)
{
	int wrap_value = 0;
	uint32_t seq_number = 0;

	int (*putc_)(int c) = uart_putc;
	int (*tx_is_blocked_)(void) = uart_buffer_full;

	if (argc > 1)
		wrap_value = atoi(argv[1]);

	if (argc > 2)
		seq_number = atoi(argv[2]);

#if defined(CONFIG_USB_CONSOLE) || defined(CONFIG_USB_CONSOLE_STREAM)
	if (argc > 3) {
		if (memcmp(argv[3], "usb", 3))
			return EC_ERROR_PARAM3;

		putc_ = usb_putc;
		tx_is_blocked_ = usb_console_tx_blocked;
	}
#endif

	chargen_ctx.wrap_value = wrap_value;
	chargen_ctx.seq_number = seq_number;
	chargen_ctx.putc = putc_;
	chargen_ctx.tx_is_blocked = tx_is_blocked_;

	return hook_call_deferred(&run_chargen_data, 0);
}
DECLARE_SAFE_CONSOLE_COMMAND(chargen, command_chargen,
#if defined(CONFIG_USB_CONSOLE) || defined(CONFIG_USB_CONSOLE_STREAM)
			     "[seq_length [num_chars [usb]]]",
#else
			     "[seq_length [num_chars]]",
#endif
			     "Generate a constant stream of characters on the "
			     "UART console,\nrepeating every 'seq_length' "
			     "characters, up to 'num_chars' total.");
#endif /* !SECTION_IS_RO */
