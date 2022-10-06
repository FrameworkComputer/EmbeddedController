/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"

ZTEST_SUITE(console_cmd_i2c_portmap, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);

ZTEST_USER(console_cmd_i2c_portmap, test_too_many_args)
{
	int rv = shell_execute_cmd(get_ec_shell(), "i2c_portmap arg1");

	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_i2c_portmap, test_get_i2c_portmap)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "i2c_portmap"), NULL);
}
