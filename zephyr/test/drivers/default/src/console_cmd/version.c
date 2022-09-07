/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "system.h"

ZTEST_USER(console_cmd_version, test_no_args)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "version"), NULL);
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0, NULL);

	/* Weakly verify some contents */
	zassert_not_null(strstr(outbuffer, "Chip:"), NULL);
	zassert_not_null(strstr(outbuffer, "Board:"), NULL);
	zassert_not_null(strstr(outbuffer, "RO:"), NULL);
	zassert_not_null(strstr(outbuffer, "RW:"), NULL);
	zassert_not_null(strstr(outbuffer, "Build:"), NULL);
}

ZTEST_SUITE(console_cmd_version, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
