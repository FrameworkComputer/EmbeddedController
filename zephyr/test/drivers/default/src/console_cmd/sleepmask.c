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

ZTEST_USER(console_cmd_sleepmask, test_no_args)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "sleepmask"), NULL);
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0, NULL);
	zassert_not_null(strstr(outbuffer, "sleep mask"), NULL);
}

ZTEST_USER(console_cmd_sleepmask, test_bad_args)
{
	const struct shell *shell_zephyr = get_ec_shell();

	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(shell_zephyr, "sleepmask whoopsie"),
		      NULL);
}

ZTEST_USER(console_cmd_sleepmask, test_set_sleep_mask_directly)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr);

	/* Set mask as 0 */
	zassert_ok(shell_execute_cmd(shell_zephyr, "sleepmask 0"), NULL);
	shell_backend_dummy_clear_output(shell_zephyr);

	/* Get mask and weakly verify mask is 0 */
	zassert_ok(shell_execute_cmd(shell_zephyr, "sleepmask"), NULL);
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_not_null(strstr(outbuffer, "0"), NULL);
	zassert_is_null(strstr(outbuffer, "1"), NULL);

	/* Set mask as 1 */
	zassert_ok(shell_execute_cmd(shell_zephyr, "sleepmask 1"), NULL);
	shell_backend_dummy_clear_output(shell_zephyr);

	/* Get mask and weakly verify mask is 1 */
	zassert_ok(shell_execute_cmd(shell_zephyr, "sleepmask"), NULL);
	zassert_not_null(strstr(outbuffer, "1"), NULL);
}

ZTEST_USER(console_cmd_sleepmask, test_enable_disable_force_sleepmask)
{
	const struct shell *shell_zephyr = get_ec_shell();

	/* Verifying enabled to disabled */

	zassert_ok(shell_execute_cmd(shell_zephyr, "sleepmask on"), NULL);

	int enabled_bits = sleep_mask & SLEEP_MASK_FORCE_NO_DSLEEP;

	zassert_ok(shell_execute_cmd(shell_zephyr, "sleepmask off"), NULL);

	int disabled_bits = sleep_mask & SLEEP_MASK_FORCE_NO_DSLEEP;

	zassert_false(enabled_bits & disabled_bits, NULL);

	/* Verifying disabled to enabled */

	zassert_ok(shell_execute_cmd(shell_zephyr, "sleepmask on"), NULL);

	enabled_bits = sleep_mask & SLEEP_MASK_FORCE_NO_DSLEEP;
	zassert_false(enabled_bits & disabled_bits, NULL);
}

static void console_cmd_sleepmask_before_after(void *test_data)
{
	ARG_UNUSED(test_data);

	enable_sleep(-1);
}

ZTEST_SUITE(console_cmd_sleepmask, drivers_predicate_post_main, NULL,
	    console_cmd_sleepmask_before_after,
	    console_cmd_sleepmask_before_after, NULL);
