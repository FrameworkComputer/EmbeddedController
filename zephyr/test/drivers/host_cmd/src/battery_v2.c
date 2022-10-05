/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "battery.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

ZTEST(host_cmd_battery_v2, test_get_static__invalid_index)
{
	struct ec_response_battery_static_info response;
	struct ec_params_battery_static_info params = {
		/* Index is out of range */
		.index = CONFIG_BATTERY_COUNT + 1,
	};
	int rv;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_BATTERY_GET_STATIC, 0, response, params);

	rv = host_command_process(&args);
	zassert_equal(EC_RES_INVALID_PARAM, rv, "Got %d", rv);
}

ZTEST(host_cmd_battery_v2, test_get_static__v0)
{
	struct ec_params_battery_static_info params = {
		.index = 0,
	};
	struct ec_response_battery_static_info response;
	int rv;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_BATTERY_GET_STATIC, 0, response, params);

	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);

	/* Validate all of the fields */
	struct battery_static_info *batt = &battery_static[0];

	zassert_equal(batt->design_capacity, response.design_capacity);
	zassert_equal(batt->design_voltage, response.design_voltage);
	zassert_equal(batt->cycle_count, response.cycle_count);
	zassert_mem_equal(batt->manufacturer_ext, response.manufacturer,
			  sizeof(response.manufacturer));
	zassert_mem_equal(batt->model_ext, response.model,
			  sizeof(response.model));
	zassert_mem_equal(batt->serial_ext, response.serial,
			  sizeof(response.serial));
	zassert_mem_equal(batt->type_ext, response.type, sizeof(response.type));
}

ZTEST(host_cmd_battery_v2, test_get_static__v1)
{
	/* Basically a repeat of the above test, but use the version 1 response
	 * struct, which allows for longer string fields
	 */

	struct ec_params_battery_static_info params = {
		.index = 0,
	};
	struct ec_response_battery_static_info_v1 response;
	int rv;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_BATTERY_GET_STATIC, 1, response, params);

	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);

	/* Validate all of the fields */
	struct battery_static_info *batt = &battery_static[0];

	zassert_equal(batt->design_capacity, response.design_capacity);
	zassert_equal(batt->design_voltage, response.design_voltage);
	zassert_equal(batt->cycle_count, response.cycle_count);
	zassert_mem_equal(batt->manufacturer_ext, response.manufacturer_ext,
			  sizeof(response.manufacturer_ext));
	zassert_mem_equal(batt->model_ext, response.model_ext,
			  sizeof(response.model_ext));
	zassert_mem_equal(batt->serial_ext, response.serial_ext,
			  sizeof(response.serial_ext));
	zassert_mem_equal(batt->type_ext, response.type_ext,
			  sizeof(response.type_ext));
}

ZTEST(host_cmd_battery_v2, test_get_dynamic__invalid_index)
{
	struct ec_response_battery_dynamic_info response;
	struct ec_params_battery_dynamic_info params = {
		/* Index is out of range */
		.index = CONFIG_BATTERY_COUNT + 1,
	};
	int rv;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_BATTERY_GET_DYNAMIC, 0, response, params);

	rv = host_command_process(&args);
	zassert_equal(EC_RES_INVALID_PARAM, rv, "Got %d", rv);
}

ZTEST(host_cmd_battery_v2, test_get_dynamic)
{
	struct ec_response_battery_dynamic_info response;
	struct ec_params_battery_dynamic_info params = {
		.index = 0,
	};
	int rv;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_BATTERY_GET_DYNAMIC, 0, response, params);

	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);

	/* Validate the data */
	struct ec_response_battery_dynamic_info *batt = &battery_dynamic[0];

	zassert_mem_equal(batt, &response, sizeof(*batt));
}

ZTEST_SUITE(host_cmd_battery_v2, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
