/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/** @brief Test functions declared in uart.h
 *
 */

#include "common.h"
#include "test_util.h"

#include <stddef.h>

extern "C" {
#include "uart.h"
}

test_static int test_uart_buffer_used(void)
{
	int32_t pre_test_buffer_used;
	int32_t delta_buffer_used;

	/* There's no direct way to verify the output character, but we can
	 * track the bytes written and */
	pre_test_buffer_used = uart_buffer_used();
	uart_tx_char_raw(NULL, (int)'a');
	delta_buffer_used = uart_buffer_used() - pre_test_buffer_used;
	TEST_EQ(delta_buffer_used, 1, "%d");

	uart_flush_output();
	TEST_EQ(uart_buffer_used(), 0, "%d");

	return EC_SUCCESS;
}

test_static int test_uart_buffer_empty(void)
{
	/* We don't know the state of the buffer now, so write a char and verify
	 * !empty */
	uart_tx_char_raw(NULL, (int)'a');
	TEST_ASSERT(!uart_buffer_empty());

	/* Now flush and ensure it is empty */
	uart_flush_output();
	TEST_ASSERT(uart_buffer_empty());

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_uart_buffer_used);
	RUN_TEST(test_uart_buffer_empty);

	test_print_result();
}
