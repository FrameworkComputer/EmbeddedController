/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_USER(charge_manager, test_port_count)
{
	struct ec_response_charge_port_count response = { 0 };

	zassert_ok(ec_cmd_charge_port_count(NULL, &response));
	zassert_equal(CHARGE_PORT_COUNT, response.port_count,
		      "Expected %d, but got %d", CHARGE_PORT_COUNT,
		      response.port_count);
}

ZTEST_USER(charge_manager, test_port_override__port_out_of_bounds)
{
	struct ec_params_charge_port_override params = { 0 };

	params.override_port = OVERRIDE_DONT_CHARGE - 1;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_pd_charge_port_override(NULL, &params));

	params.override_port = CHARGE_PORT_COUNT;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_pd_charge_port_override(NULL, &params));
}

ZTEST_USER(charge_manager, test_port_override__0_from_off)
{
	struct ec_params_charge_port_override params = { 0 };

	params.override_port = 0;
	zassert_ok(ec_cmd_pd_charge_port_override(NULL, &params));
}

ZTEST_USER(charge_manager, test_charge_state_get_debug_params)
{
	struct ec_params_charge_state params = {
		.cmd = CHARGE_STATE_CMD_GET_PARAM,
	};
	struct ec_response_charge_state response = { 0 };

	/* Check that the following get commands work on these debug parameters.
	 * The values being asserted are the default values when nothing is
	 * plugged in. This should be enough since the test only needs to verify
	 * that the command gets the current value. Tests that verify the
	 * charging behavior exist elsewhere (under
	 * default/src/integration/usbc).
	 */
	params.get_param.param = CS_PARAM_DEBUG_CTL_MODE;
	zassert_ok(ec_cmd_charge_state(NULL, &params, &response));
	zassert_equal(0, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_MANUAL_CURRENT;
	zassert_ok(ec_cmd_charge_state(NULL, &params, &response));
	zassert_equal(0xffffffff, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_MANUAL_VOLTAGE;
	zassert_ok(ec_cmd_charge_state(NULL, &params, &response));
	zassert_equal(0xffffffff, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_SEEMS_DEAD;
	zassert_ok(ec_cmd_charge_state(NULL, &params, &response));
	zassert_equal(0, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_SEEMS_DISCONNECTED;
	zassert_ok(ec_cmd_charge_state(NULL, &params, &response));
	zassert_equal(0, response.get_param.value);

	params.get_param.param = CS_PARAM_DEBUG_MAX;
	zassert_equal(EC_ERROR_INVAL,
		      ec_cmd_charge_state(NULL, &params, &response));
}

ZTEST_SUITE(charge_manager, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
