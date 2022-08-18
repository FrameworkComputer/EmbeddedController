/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "builtin/stdio.h"
#include "test/drivers/test_state.h"
#include "console.h"
#include "uart.h"
#include "ec_commands.h"

ZTEST_USER(console, printf_overflow)
{
	char buffer[10];

	zassert_equal(-EC_ERROR_OVERFLOW,
		      crec_snprintf(buffer, 4, "1234567890"), NULL);
	zassert_equal(0, strcmp(buffer, "123"), "got '%s'", buffer);
	zassert_equal(-EC_ERROR_OVERFLOW,
		      crec_snprintf(buffer, 4, "%%%%%%%%%%"), NULL);
	zassert_equal(0, strcmp(buffer, "%%%"), "got '%s'", buffer);
}

/* This test is identical to test_buf_notify_null in
 * test/console_edit.c. Please keep them in sync to verify that
 * uart_console_read_buffer works identically in legacy EC and zephyr.
 */
ZTEST_USER(console, buf_notify_null)
{
	char buffer[100];
	uint16_t write_count;

	/* Flush the console buffer before we start. */
	zassert_ok(uart_console_read_buffer_init(), NULL);

	/* Write a nul char to the buffer. */
	console_buf_notify_chars("ab\0c", 4);

	/* Check if the nul is present in the buffer. */
	zassert_ok(uart_console_read_buffer_init(), NULL);
	zassert_ok(uart_console_read_buffer(CONSOLE_READ_RECENT, buffer,
					    sizeof(buffer), &write_count),
		   NULL);
	zassert_equal(0, strncmp(buffer, "abc", 4), "got '%s'", buffer);
	zassert_equal(write_count, 4, "got %d", write_count);
}

ZTEST_SUITE(console, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
