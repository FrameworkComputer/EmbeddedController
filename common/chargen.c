/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "console.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "watchdog.h"

/*
 * Microseconds time to drain entire UART_TX console buffer at 115200 b/s, 10
 * bits per character.
 */
#define BUFFER_DRAIN_TIME_US (1000000UL * 10 * CONFIG_UART_TX_BUF_SIZE / 115200)
/*
 * Generate a stream of characters on the UART console.
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
static int command_chargen(int argc, char **argv)
{
	int wrap_value = 0;
	int wrap_counter = 0;
	uint8_t c;
	uint32_t seq_counter = 0;
	uint32_t seq_number = 0;
	timestamp_t prev_watchdog_time;

	while (uart_getc() != -1)
		; /* Drain received characters, if any. */

	if (argc > 1)
		wrap_value = atoi(argv[1]);

	if (argc > 2)
		seq_number = atoi(argv[2]);

	c = '0';
	prev_watchdog_time = get_time();
	while (uart_getc() != 'x') {
		timestamp_t current_time;

		while (uart_buffer_full()) {
			/*
			 * Let's sleep enough time to drain half of TX
			 * buffer.
			 */
			usleep(BUFFER_DRAIN_TIME_US/2);

			current_time = get_time();

			if ((current_time.val - prev_watchdog_time.val) <
			    (CONFIG_WATCHDOG_PERIOD_MS * 1000 / 2))
				continue;

			watchdog_reload();
			prev_watchdog_time.val = current_time.val;
		}

		uart_putc(c++);

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
		else if (c  == ('9' + 1))
			c = 'A';
	}

	uart_putc('\n');
	return 0;
}
DECLARE_SAFE_CONSOLE_COMMAND(chargen, command_chargen,
			     "[seq_length [num_chars]]",
			     "Generate a constant stream of characters on the "
			     "UART console,\nrepeating every 'seq_length' "
			     "characters, up to 'num_chars' total."
	);
