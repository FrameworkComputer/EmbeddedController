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

ZTEST_USER(host_cmd_get_pd_port_caps, test_good_index)
{
	struct ec_params_get_pd_port_caps params = { .port = 0 };
	struct ec_response_get_pd_port_caps response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_GET_PD_PORT_CAPS, 0, response, params);

	zassert_ok(host_command_process(&args),
		   "Failed to process get_pd_port_caps for port %d",
		   params.port);

	/* Verify standard Chromebook responses for these fields */
	zassert_equal(response.pd_power_role_cap, EC_PD_POWER_ROLE_DUAL,
		      "Bad dual role");
	zassert_equal(response.pd_try_power_role_cap,
		      EC_PD_TRY_POWER_ROLE_SOURCE, "Bad try role");
	zassert_equal(response.pd_data_role_cap, EC_PD_DATA_ROLE_DUAL,
		      "Bad data role");
	zassert_equal(response.pd_port_location, EC_PD_PORT_LOCATION_UNKNOWN,
		      "Unexpected port location");
}

ZTEST_USER(host_cmd_get_pd_port_caps, test_bad_index)
{
	struct ec_params_get_pd_port_caps params = { .port = 32 };
	struct ec_response_get_pd_port_caps response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_GET_PD_PORT_CAPS, 0, response, params);

	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM,
		      "Failed to fail get_pd_port_caps for port %d",
		      params.port);
}

static void host_cmd_get_pd_port_caps_begin(void *data)
{
	ARG_UNUSED(data);

	/* Assume we have at least one USB-C port */
	zassume_true(board_get_usb_pd_port_count() > 0,
		     "Insufficient TCPCs found");
}

ZTEST_SUITE(host_cmd_get_pd_port_caps, drivers_predicate_post_main, NULL,
	    host_cmd_get_pd_port_caps_begin, NULL, NULL);
