/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h> /* nocheck */
#include <zephyr/ztest.h>

#include "battery.h"
#include "console.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

ZTEST_SUITE(console_cmd_pwr_avg, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST_USER(console_cmd_pwr_avg, test_too_many_args)
{
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(get_ec_shell(), "pwr_avg 5"));
}

ZTEST_USER(console_cmd_pwr_avg, test_printout)
{
	int mv = battery_get_avg_voltage();
	int ma = battery_get_avg_current();
	char expected_output[1024];
	const char *buffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "pwr_avg"));

	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	sprintf(expected_output, "mv = %d", mv);
	zassert_true(strstr(buffer, expected_output));

	sprintf(expected_output, "ma = %d", ma);
	zassert_true(strstr(buffer, expected_output));

	sprintf(expected_output, "mw = %d", mv * ma / 1000);
	zassert_true(strstr(buffer, expected_output));
}
