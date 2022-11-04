/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <cstring>
#include <string>

#include <zephyr/ztest.h>

#include "common.h"
#include "printf.h"
#include "uart.h"

ZTEST_SUITE(uart_printf, nullptr, nullptr, nullptr, nullptr, nullptr);

ZTEST(uart_printf, test_uart_putc)
{
	int return_vals[] = { 0, -1 };

	SET_RETURN_SEQ(uart_tx_char_raw, return_vals, 2);

	zassert_ok(uart_putc(5));
	zassert_equal(EC_ERROR_OVERFLOW, uart_putc(5));
}

ZTEST(uart_printf, test_uart_put_success)
{
	const std::string test_string = "test string";
	std::string output_string;

	/* Print the whole string */
	zassert_equal(test_string.size(),
		      static_cast<size_t>(uart_put(test_string.c_str(),
						   test_string.size())));
	zassert_equal(test_string.size(), uart_tx_char_raw_fake.call_count);

	/* Copy the history so it's easier to assert */
	for (size_t i = 0; i < test_string.size(); ++i) {
		output_string += uart_tx_char_raw_fake.arg1_history[i];
	}

	/* Verify that the string was passed to uart_tx_char_raw() */
	zassert_equal(test_string, output_string);
}

ZTEST(uart_printf, test_uart_put_fail_tx)
{
	const char test_string[] = "\n";

	uart_tx_char_raw_fake.return_val = -1;

	/* Try printing the newline */
	zassert_equal(0, uart_put(test_string, 1));
	zassert_equal(1, uart_tx_char_raw_fake.call_count);
	zassert_equal('\r', uart_tx_char_raw_fake.arg1_val);
}

ZTEST(uart_printf, test_uart_puts_fail_tx)
{
	const char test_string[] = "\n\0";

	uart_tx_char_raw_fake.return_val = -1;

	/* Try printing the newline */
	zassert_equal(EC_ERROR_OVERFLOW, uart_puts(test_string));
	zassert_equal(1, uart_tx_char_raw_fake.call_count);
	zassert_equal('\r', uart_tx_char_raw_fake.arg1_val);
}

ZTEST(uart_printf, test_uart_put_raw_fail_tx)
{
	const char test_string[] = "\n";

	uart_tx_char_raw_fake.return_val = -1;

	/* Try printing the newline */
	zassert_equal(0, uart_put_raw(test_string, 1));
	zassert_equal(1, uart_tx_char_raw_fake.call_count);
	zassert_equal('\n', uart_tx_char_raw_fake.arg1_val);
}

static int vfnprintf_custom_fake_expect_int_arg;
static int vfnprintf_custom_fake(vfnprintf_addchar_t, void *, const char *,
				 va_list alist)
{
	zassert_equal(vfnprintf_custom_fake_expect_int_arg, va_arg(alist, int));
	return 0;
}
ZTEST(uart_printf, test_uart_printf)
{
	const char test_format[] = "d=%d";

	vfnprintf_custom_fake_expect_int_arg = 5;
	vfnprintf_fake.custom_fake = vfnprintf_custom_fake;

	zassert_ok(
		uart_printf(test_format, vfnprintf_custom_fake_expect_int_arg));
	zassert_equal(1, vfnprintf_fake.call_count);
	zassert_equal(test_format, vfnprintf_fake.arg2_val);
}
