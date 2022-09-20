/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "console.h"
#include "test/drivers/test_state.h"
#include "timer.h"
#include "usbc_ppc.h"

/* Tests for USBC PPC Common Code */

ZTEST(usbc_ppc, test_ppc_dump__no_args)
{
	const struct shell *shell_zephyr = get_ec_shell();

	/* There's no output from the cmd given < 2 args */
	zassert_equal(shell_execute_cmd(shell_zephyr, "ppc_dump"),
		      EC_ERROR_PARAM_COUNT);
}

ZTEST(usbc_ppc, test_ppc_dump__bad_args)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_equal(shell_execute_cmd(shell_zephyr, "ppc_dump -1"),
		      EC_ERROR_INVAL);
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_not_null(strstr(outbuffer, "Invalid port!"));
}

ZTEST(usbc_ppc, test_ppc_dump__good_args)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "ppc_dump 0"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0);

	/* Weakly verify something reasonable was output to console */
	zassert_not_null(strstr(outbuffer, " = 0x"));
}

ZTEST_SUITE(usbc_ppc, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
