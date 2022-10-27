/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "uart.h"
#include "test/drivers/test_state.h"

void uart_callback(const struct device *dev, void *user_data);
void bypass_cb(const struct shell *shell, uint8_t *data, size_t len);

static const struct device *uart_shell_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

static void shell_before(void *f)
{
	ARG_UNUSED(f);
	uart_shell_start();
	k_msleep(500);
}

ZTEST_SUITE(shell, drivers_predicate_post_main, NULL, shell_before, NULL, NULL);

ZTEST(shell, test_shell_stop_read_raw_data)
{
	const char uart_data[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	uart_shell_stop();
	k_msleep(500);
	zassert_true(uart_irq_tx_ready(uart_shell_dev) != 0);

	uart_clear_input();

	/* Run the callback once, should be empty */
	uart_callback(uart_shell_dev, NULL);

	zassert_equal(-1, uart_getc());

	uart_clear_input();

	/* Manually fill the buffer */
	for (size_t i = 0; i < CONFIG_UART_RX_BUF_SIZE; ++i) {
		bypass_cb(get_ec_shell(),
			  (uint8_t *)&uart_data[i % ARRAY_SIZE(uart_data)], 1);
	}

	/* Run the callback again to make sure we didn't lose any data */
	uart_callback(uart_shell_dev, NULL);

	for (size_t i = 0; i < CONFIG_UART_RX_BUF_SIZE; ++i) {
		int c = uart_getc();

		zassert_equal(
			(int)uart_data[i % ARRAY_SIZE(uart_data)], c,
			"Expected %uth character to be %c, but uart_getc() returned %c",
			i, uart_data[i % ARRAY_SIZE(uart_data)], (char)c);
	}
}
