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

ZTEST_USER(charge_manager, test_charge_state_get_debug_params)
{
	struct ec_params_charge_state params = {
		.cmd = CHARGE_STATE_CMD_GET_PARAM,
	};
	struct ec_response_charge_state response = { 0 };
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_CHARGE_STATE, 0, response, params);

	/* Check that the following get commands work on these debug parameters.
	 * The values being asserted are the default values when nothing is
	 * plugged in. This should be enough since the test only needs to verify
	 * that the command gets the current value. Tests that verify the
	 * charging behavior exist elsewhere (under
	 * default/src/integration/usbc).
	 */
	params.get_param.param = CS_PARAM_DEBUG_CTL_MODE;
	zassert_ok(host_command_process(&args));
	zassert_equal(0, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_MANUAL_CURRENT;
	zassert_ok(host_command_process(&args));
	zassert_equal(0xffffffff, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_MANUAL_VOLTAGE;
	zassert_ok(host_command_process(&args));
	zassert_equal(0xffffffff, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_SEEMS_DEAD;
	zassert_ok(host_command_process(&args));
	zassert_equal(0, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_SEEMS_DISCONNECTED;
	zassert_ok(host_command_process(&args));
	zassert_equal(0, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_BATT_REMOVED;
	zassert_ok(host_command_process(&args));
	zassert_equal(0, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_MAX;
	zassert_equal(EC_ERROR_INVAL, host_command_process(&args));
}

ZTEST_SUITE(charge_manager, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
