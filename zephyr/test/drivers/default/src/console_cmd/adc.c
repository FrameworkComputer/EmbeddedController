/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

/* Default adc command, lists out channels */
ZTEST_USER(console_cmd_adc, test_adc_noname)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "adc"),
		   "Failed default print");
}

/* adc with named channels */
ZTEST_USER(console_cmd_adc, test_adc_named_channels)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "adc charger"),
		   "Failed to get charger adc channel.");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "adc ddr-soc"),
		   "Failed to get ddr-soc adc channel.");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "adc fan"),
		   "Failed to get fan adc channel.");
	zassert_ok(shell_execute_cmd(get_ec_shell(), "adc psys"),
		   "Failed to get psys adc channel.");
}

/* adc with unknown channel */
ZTEST_USER(console_cmd_adc, test_adc_wrong_name)
{
	int rv = shell_execute_cmd(get_ec_shell(), "adc fish");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_SUITE(console_cmd_adc, NULL, NULL, NULL, NULL, NULL);
