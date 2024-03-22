/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "system.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "uart.h"

#include <stdio.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/uart/serial_test.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

const char expected_output[] =
	"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n";

/* |chargen| is only supported in RW */
#if defined(SECTION_IS_RW)
ZTEST_USER(console_cmd_chargen, test_no_args)
{
	const struct device *uart_shell_dev =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	const struct shell *shell_zephyr = get_ec_shell();
	uint32_t nread;
	char read_buf[126] = { 0 };

	shell_backend_dummy_clear_output(shell_zephyr);

	uart_clear_input();

	zassert_ok(shell_execute_cmd(shell_zephyr, "chargen 62 124"), NULL);
	k_sleep(K_MSEC(500));

	nread = serial_vnd_read_out_data(uart_shell_dev, read_buf,
					 sizeof(read_buf));
	zassert_true(nread == sizeof(read_buf));
	zassert_true(memcmp(read_buf, expected_output, nread));
}
#endif

ZTEST_SUITE(console_cmd_chargen, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
