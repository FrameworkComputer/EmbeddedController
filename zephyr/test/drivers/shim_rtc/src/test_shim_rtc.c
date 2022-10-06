/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

ZTEST_USER(rtc_shim, test_hc_rtc_set_get_val)
{
	struct ec_params_rtc set_value;
	struct ec_params_rtc get_value;
	struct host_cmd_handler_args set_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_RTC_SET_VALUE, 0, set_value);
	struct host_cmd_handler_args get_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_RTC_GET_VALUE, 0, get_value);

	/* Initially set/get arbitrary value */
	set_value.time = 1337;
	zassert_ok(host_command_process(&set_args));
	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_value.time, set_value.time);

	/* One more time to be sure the test is creating the value change */
	set_value.time = 1776;
	zassert_ok(host_command_process(&set_args));
	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_value.time, set_value.time);
}

ZTEST_SUITE(rtc_shim, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
