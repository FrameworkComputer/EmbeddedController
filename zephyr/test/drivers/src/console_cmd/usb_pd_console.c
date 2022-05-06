/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <shell/shell.h>
#include <ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

static void console_cmd_usb_pd_after(void *fixture)
{
	ARG_UNUSED(fixture);

	/* TODO (b/230059737) */
	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(1));
	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(10));
}

ZTEST_SUITE(console_cmd_usb_pd, drivers_predicate_post_main, NULL, NULL,
	    console_cmd_usb_pd_after, NULL);

ZTEST_USER(console_cmd_usb_pd, test_too_few_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_dump)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd dump 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd dump 4");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd dump -4");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd dump x");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_trysrc)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd trysrc 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd trysrc 2");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd trysrc 5");
	zassert_equal(rv, EC_ERROR_PARAM3, "Expected %d, but got %d",
		      EC_ERROR_PARAM3, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_version)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd version");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_bad_port)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 5");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 5 tx");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_tx)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 tx");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_charger)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 charger");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_dev)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dev");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dev 20");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dev x");
	zassert_equal(rv, EC_ERROR_PARAM3, "Expected %d, but got %d",
		      EC_ERROR_PARAM3, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_disable)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 disable");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_enable)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 enable");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_hard)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 hard");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_soft)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 soft");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_swap)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap power");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap data");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap vconn");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap x");
	zassert_equal(rv, EC_ERROR_PARAM3, "Expected %d, but got %d",
		      EC_ERROR_PARAM3, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_dualrole)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole on");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole off");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole freeze");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole sink");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole source");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole x");
	zassert_equal(rv, EC_ERROR_PARAM4, "Expected %d, but got %d",
		      EC_ERROR_PARAM4, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_state)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 state");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_srccaps)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 srccaps");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_timer)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 timer");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}
