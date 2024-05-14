/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "mock_pdc_power_mgmt.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0
#define SLEEP_MS 120

static void console_cmd_pdc_setup(void)
{
	zassume(TEST_PORT < CONFIG_USB_PD_PORT_MAX_COUNT,
		"TEST_PORT is invalid");
}

static void console_cmd_pdc_reset(void *fixture)
{
	helper_reset_pdc_power_mgmt_fakes();
}

ZTEST_SUITE(console_cmd_pdc, NULL, console_cmd_pdc_setup, console_cmd_pdc_reset,
	    console_cmd_pdc_reset, NULL);

ZTEST_USER(console_cmd_pdc, test_no_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pdc");
	zassert_equal(rv, SHELL_CMD_HELP_PRINTED, "Expected %d, but got %d",
		      SHELL_CMD_HELP_PRINTED, rv);
}

ZTEST_USER(console_cmd_pdc, test_cable_prop)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pdc cable_prop 99");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pdc cable_prop 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_pdc, test_trysrc)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pdc trysrc enable");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pdc trysrc 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	k_sleep(K_MSEC(SLEEP_MS));

	rv = shell_execute_cmd(get_ec_shell(), "pdc trysrc 1");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	k_sleep(K_MSEC(SLEEP_MS));

	rv = shell_execute_cmd(get_ec_shell(), "pdc trysrc 2");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);
}
