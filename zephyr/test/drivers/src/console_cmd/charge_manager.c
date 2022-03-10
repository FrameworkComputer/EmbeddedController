/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "charge_manager.h"
#include "console.h"
#include "test_state.h"

static void console_cmd_charge_manager_after(void *state)
{
	ARG_UNUSED(state);
	shell_execute_cmd(get_ec_shell(), "chgoverride -1");
}

ZTEST_SUITE(console_cmd_charge_manager, drivers_predicate_post_main, NULL, NULL,
	    console_cmd_charge_manager_after, NULL);

/**
 * Test the chgsup (charge supplier info) command. This command only prints to
 * console some information which is not yet possible to verify. So just check
 * that the console command ran successfully.
 */
ZTEST_USER(console_cmd_charge_manager, test_chgsup)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgsup"), NULL);
}

/**
 * Test chgoverride command with no arguments. This should just print the
 * current override port.
 */
ZTEST_USER(console_cmd_charge_manager, test_chgoverride_missing_port)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgoverride"), NULL);
}

ZTEST_USER(console_cmd_charge_manager, test_chgoverride_off_from_off)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgoverride -1"), NULL);
	zassert_equal(charge_manager_get_override(), OVERRIDE_OFF, NULL);
}

ZTEST_USER(console_cmd_charge_manager, test_chgoverride_disable_from_off)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgoverride -2"), NULL);
	zassert_equal(charge_manager_get_override(), OVERRIDE_DONT_CHARGE,
		      NULL);
}
