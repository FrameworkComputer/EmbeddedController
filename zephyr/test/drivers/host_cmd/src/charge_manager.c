/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "charge_manager.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

ZTEST_USER(charge_manager, test_port_count)
{
	struct ec_response_charge_port_count response = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_CHARGE_PORT_COUNT, 0, response);

	zassert_ok(host_command_process(&args));
	zassert_equal(CHARGE_PORT_COUNT, response.port_count,
		      "Expected %d, but got %d", CHARGE_PORT_COUNT,
		      response.port_count);
}

ZTEST_USER(charge_manager, test_port_override__port_out_of_bounds)
{
	struct ec_params_charge_port_override params = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_PD_CHARGE_PORT_OVERRIDE, 0, params);

	params.override_port = OVERRIDE_DONT_CHARGE - 1;
	zassert_equal(EC_RES_INVALID_PARAM, host_command_process(&args));

	params.override_port = CHARGE_PORT_COUNT;
	zassert_equal(EC_RES_INVALID_PARAM, host_command_process(&args));
}

ZTEST_USER(charge_manager, test_port_override__0_from_off)
{
	struct ec_params_charge_port_override params = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_PD_CHARGE_PORT_OVERRIDE, 0, params);

	params.override_port = 0;
	zassert_ok(host_command_process(&args));
}

ZTEST_SUITE(charge_manager, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
