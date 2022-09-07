/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "system.h"

ZTEST_USER(console_cmd_sysinfo, test_no_args)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "sysinfo"), NULL);
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0, NULL);

	/* Weakly verify some contents */
	zassert_not_null(strstr(outbuffer, "Reset flags:"), NULL);
	zassert_not_null(strstr(outbuffer, "Copy:"), NULL);
	zassert_not_null(strstr(outbuffer, "Jumped:"), NULL);
	zassert_not_null(strstr(outbuffer, "Recovery:"), NULL);
	zassert_not_null(strstr(outbuffer, "Flags:"), NULL);
}

ZTEST_USER(console_cmd_sysinfo, test_no_args__sys_locked)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	/* System unlocked */
	shell_backend_dummy_clear_output(shell_zephyr);
	system_is_locked_fake.return_val = false;

	zassert_ok(shell_execute_cmd(shell_zephyr, "sysinfo"), NULL);
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0, NULL);
	zassert_not_null(strstr(outbuffer, "unlocked"), NULL);

	/* System locked */
	shell_backend_dummy_clear_output(shell_zephyr);
	system_is_locked_fake.return_val = true;

	zassert_true(buffer_size > 0, NULL);
	zassert_ok(shell_execute_cmd(shell_zephyr, "sysinfo"), NULL);

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_not_null(strstr(outbuffer, "locked"), NULL);

	/* Verify system_is_locked in sysinfo cmd response remains */
	shell_backend_dummy_clear_output(shell_zephyr);
	system_is_locked_fake.return_val = false;

	zassert_ok(shell_execute_cmd(shell_zephyr, "sysinfo"), NULL);
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0, NULL);
	zassert_not_null(strstr(outbuffer, "locked"), NULL);
}

static void console_cmd_sysinfo_before_after(void *test_data)
{
	ARG_UNUSED(test_data);

	system_common_reset_state();
}

ZTEST_SUITE(console_cmd_sysinfo, drivers_predicate_post_main, NULL,
	    console_cmd_sysinfo_before_after, console_cmd_sysinfo_before_after,
	    NULL);
