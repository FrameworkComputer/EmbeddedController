/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

static void console_cmd_cutoff_after(void *unused)
{
	ARG_UNUSED(unused);
	set_ac_enabled(true);
	hook_notify(HOOK_AC_CHANGE);
	k_msleep(500);
}

ZTEST_SUITE(console_cmd_cutoff, drivers_predicate_post_main, NULL, NULL,
	    console_cmd_cutoff_after, NULL);

ZTEST_USER(console_cmd_cutoff, test_sb_cutoff)
{
	int rv = shell_execute_cmd(get_ec_shell(), "cutoff");

	zassert_equal(EC_RES_SUCCESS, rv, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);
	zassert_true(battery_cutoff_in_progress(), NULL);
	zassert_true(WAIT_FOR(battery_is_cut_off(), 2105000, k_msleep(250)),
		     NULL);
}

ZTEST_USER(console_cmd_cutoff, test_sb_cutoff_timeout)
{
	int rv;

	set_ac_enabled(false);

	rv = shell_execute_cmd(get_ec_shell(), "cutoff");
	zassert_equal(EC_RES_SUCCESS, rv, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);
	zassert_true(battery_cutoff_in_progress());

	zassert_false(WAIT_FOR(battery_is_cut_off(), 510000, k_msleep(250)));
}

ZTEST_USER(console_cmd_cutoff, test_invalid_arg1)
{
	int rv = shell_execute_cmd(get_ec_shell(), "cutoff bad_arg");

	zassert_equal(EC_ERROR_INVAL, rv, "Expected %d, but got %d",
		      EC_ERROR_INVAL, rv);
	zassert_false(battery_is_cut_off(), NULL);
}

ZTEST_USER(console_cmd_cutoff, test_at_shutdown)
{
	int rv = shell_execute_cmd(get_ec_shell(), "cutoff at-shutdown");

	zassert_equal(EC_RES_SUCCESS, rv, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);
	zassert_false(battery_is_cut_off(), NULL);
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_true(WAIT_FOR(battery_is_cut_off(), 2105000, k_msleep(250)),
		     NULL);
}

ZTEST_USER(console_cmd_cutoff, test_clear_pending_shutdown)
{
	int rv = shell_execute_cmd(get_ec_shell(), "cutoff at-shutdown");

	zassert_true(extpower_is_present(), NULL);
	zassert_equal(EC_RES_SUCCESS, rv, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);

	/* Triggering the AC_CHANGE hook will cancel the pending cutoff */
	hook_notify(HOOK_AC_CHANGE);

	/* The shutdown will no longer cutoff the battery */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_false(WAIT_FOR(battery_is_cut_off(), 2105000, k_msleep(250)),
		      NULL);
}

ZTEST_USER(console_cmd_cutoff, test_ac_change_exits_cutoff)
{
	int rv;

	set_ac_enabled(false);

	rv = shell_execute_cmd(get_ec_shell(), "cutoff");
	zassert_equal(EC_RES_SUCCESS, rv, "Expected %d, but got %d",
		      EC_RES_SUCCESS, rv);

	set_ac_enabled(true);
	zassert_false(battery_is_cut_off(), NULL);
}
