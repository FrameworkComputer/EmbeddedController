/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "uart.h"

#include <zephyr/drivers/uart/serial_test.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/ztest.h>

const struct device *uart_shell_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

/*
 * There's no API way to unititialize a device,
 * just set it directly on device object.
 */
static void uart_set_init_state(bool state)
{
	uart_shell_dev->state->initialized = state;
}

static void uart_clear(void)
{
	char output[128];
	while (serial_vnd_out_data_size_get(uart_shell_dev)) {
		serial_vnd_read_out_data(uart_shell_dev, output,
					 sizeof(output));
	}
}

static bool check_uart_output(const char *expected_output)
{
	char output[128] = { 0 };
	zassert_true(strlen(expected_output) < sizeof(output));
	serial_vnd_read_out_data(uart_shell_dev, output, sizeof(output));
	return strstr(output, expected_output) != NULL;
}

/* Make sure uart is initialized and clear before each test*/
static void panic_output_print_before(void *data)
{
	zassert_true(uart_init_done(), NULL);
	uart_clear();
}

/* Ensure panic_printf prints string to uart */
ZTEST_USER(panic_output_print, test_panic_printf)
{
	panic_printf("panic_printf: %s\n", "test");
	zassert_true(check_uart_output("panic_printf: test"));
}

/* Ensure panic_puts prints string to uart */
ZTEST_USER(panic_output_print, test_panic_puts)
{
	panic_puts("panic_puts\n");
	zassert_true(check_uart_output("panic_puts"));
}

/*
 * Same as test_panic_printf, except uart is not initialized. Should not print.
 */
ZTEST_USER(panic_output_print, test_panic_printf_noinit)
{
	uart_set_init_state(false);
	panic_printf("panic_printf: %s\n", "test");
	uart_set_init_state(true);
	zassert_false(check_uart_output("panic_printf: test"));
}

/* Same as test_panic_puts, except uart is not initialized. Should not print. */
ZTEST_USER(panic_output_print, test_panic_puts_noinit)
{
	uart_set_init_state(false);
	panic_puts("panic_puts\n");
	uart_set_init_state(true);
	zassert_false(check_uart_output("panic_puts"));
}

ZTEST_SUITE(panic_output_print, NULL, NULL, panic_output_print_before, NULL,
	    NULL);
