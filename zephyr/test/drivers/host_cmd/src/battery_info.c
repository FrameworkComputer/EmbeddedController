/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST(host_cmd_battery_info, test_get_static__invalid_index)
{
	struct ec_response_battery_static_info response;
	struct ec_params_battery_static_info params = {
		/* Index is out of range */
		.index = CONFIG_BATTERY_COUNT + 1,
	};
	int rv;

	rv = ec_cmd_battery_get_static(NULL, &params, &response);
	zassert_equal(EC_RES_INVALID_PARAM, rv, "Got %d", rv);
}

ZTEST(host_cmd_battery_info, test_get_static__v0)
{
	struct ec_params_battery_static_info params = {
		.index = 0,
	};
	struct ec_response_battery_static_info response;
	int rv;

	rv = ec_cmd_battery_get_static(NULL, &params, &response);
	zassert_ok(rv, "Got %d", rv);

	/* Validate all of the fields */
	struct battery_static_info *batt = &battery_static[0];

	zassert_equal(batt->design_capacity, response.design_capacity);
	zassert_equal(batt->design_voltage, response.design_voltage);
	zassert_equal(batt->cycle_count, response.cycle_count);
	zassert_mem_equal(batt->manufacturer_ext, response.manufacturer,
			  sizeof(response.manufacturer) - 1, "%s != %s",
			  batt->manufacturer_ext, response.manufacturer);
	zassert_equal(0,
		      response.manufacturer[sizeof(response.manufacturer) - 1],
		      "Missing NULL");
	zassert_mem_equal(batt->model_ext, response.model,
			  sizeof(response.model) - 1, "%s != %s",
			  batt->model_ext, response.model);
	zassert_equal(0, response.model[sizeof(response.model) - 1],
		      "Missing NULL");
	zassert_mem_equal(batt->serial_ext, response.serial,
			  sizeof(response.serial));
	zassert_mem_equal(batt->type_ext, response.type, sizeof(response.type));
}

ZTEST(host_cmd_battery_info, test_get_static__v1)
{
	/* Basically a repeat of the above test, but use the version 1 response
	 * struct, which allows for longer string fields
	 */

	struct ec_params_battery_static_info params = {
		.index = 0,
	};
	struct ec_response_battery_static_info_v1 response;
	int rv;

	rv = ec_cmd_battery_get_static_v1(NULL, &params, &response);
	zassert_ok(rv, "Got %d", rv);

	/* Validate all of the fields */
	struct battery_static_info *batt = &battery_static[0];

	zassert_equal(batt->design_capacity, response.design_capacity);
	zassert_equal(batt->design_voltage, response.design_voltage);
	zassert_equal(batt->cycle_count, response.cycle_count);
	zassert_mem_equal(batt->manufacturer_ext, response.manufacturer_ext,
			  sizeof(response.manufacturer_ext) - 1, "%s != %s",
			  batt->manufacturer_ext, response.manufacturer_ext);
	zassert_equal(
		0,
		response.manufacturer_ext[sizeof(response.manufacturer_ext) - 1],
		"Missing NULL");
	zassert_mem_equal(batt->model_ext, response.model_ext,
			  sizeof(response.model_ext) - 1, "%s != %s",
			  batt->model_ext, response.model_ext);
	zassert_equal(0, response.model_ext[sizeof(response.model_ext) - 1],
		      "Missing NULL");
	zassert_mem_equal(batt->serial_ext, response.serial_ext,
			  sizeof(response.serial_ext));
	zassert_mem_equal(batt->type_ext, response.type_ext,
			  sizeof(response.type_ext));
}

ZTEST(host_cmd_battery_info, test_get_static__v2)
{
	/* As above, now using the v2 response for longer strings yet. */
	struct ec_params_battery_static_info params = {
		.index = 0,
	};
	struct ec_response_battery_static_info_v2 response;
	int rv;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_BATTERY_GET_STATIC, 2, response, params);

	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);
	zassert_equal(args.response_size, sizeof(response));

	/* Validate all of the fields */
	struct battery_static_info *batt = &battery_static[0];

	zassert_equal(batt->design_capacity, response.design_capacity);
	zassert_equal(batt->design_voltage, response.design_voltage);
	zassert_equal(batt->cycle_count, response.cycle_count);
	zassert_mem_equal(batt->manufacturer_ext, response.manufacturer,
			  sizeof(response.manufacturer));
	zassert_mem_equal(batt->model_ext, response.device_name,
			  sizeof(response.device_name));
	zassert_mem_equal(batt->serial_ext, response.serial,
			  sizeof(response.serial));
	zassert_mem_equal(batt->type_ext, response.chemistry,
			  sizeof(response.chemistry));
}

ZTEST(host_cmd_battery_info, test_get_static__v3)
{
	/* Same as v2, adds new manuf_info field. */
	struct ec_params_battery_static_info params = {
		.index = 0,
	};
	struct ec_response_battery_static_info_v3 response;
	int rv;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_BATTERY_GET_STATIC, 3, response, params);

	rv = host_command_process(&args);
	zassert_ok(rv, "Got %d", rv);
	zassert_equal(args.response_size, sizeof(response));

	/* Validate all of the fields */
	struct battery_static_info *batt = &battery_static[0];

	zassert_equal(batt->design_capacity, response.design_capacity);
	zassert_equal(batt->design_voltage, response.design_voltage);
	zassert_equal(batt->cycle_count, response.cycle_count);
	zassert_mem_equal(batt->manufacturer_ext, response.manufacturer,
			  sizeof(response.manufacturer));
	zassert_mem_equal(batt->model_ext, response.device_name,
			  sizeof(response.device_name));
	zassert_mem_equal(batt->serial_ext, response.serial,
			  sizeof(response.serial));
	zassert_mem_equal(batt->type_ext, response.chemistry,
			  sizeof(response.chemistry));
	zassert_mem_equal(batt->manuf_info, response.manuf_info,
			  sizeof(response.manuf_info));
}

ZTEST(host_cmd_battery_info, test_get_dynamic__invalid_index)
{
	struct ec_response_battery_dynamic_info response;
	struct ec_params_battery_dynamic_info params = {
		/* Index is out of range */
		.index = CONFIG_BATTERY_COUNT + 1,
	};
	int rv;

	rv = ec_cmd_battery_get_dynamic(NULL, &params, &response);
	zassert_equal(EC_RES_INVALID_PARAM, rv, "Got %d", rv);
}

ZTEST(host_cmd_battery_info, test_get_dynamic)
{
	struct ec_response_battery_dynamic_info response0;
	struct ec_response_battery_dynamic_info_v1 response1;
	struct ec_params_battery_dynamic_info params = {
		.index = 0,
	};
	int rv;
	const struct ec_response_battery_dynamic_info_v1 *batt =
		&battery_dynamic[0];

	memset(&response0, 0, sizeof(response0));
	rv = ec_cmd_battery_get_dynamic(NULL, &params, &response0);
	zassert_ok(rv, "Got %d", rv);
	/*
	 * BATTERY_GET_DYNAMIC v0 only returns part of the battery info
	 * struct as new fields have been appended in later versions.
	 */
	zassert_mem_equal(batt, &response0, sizeof(response0));

	memset(&response1, 0, sizeof(response1));
	rv = ec_cmd_battery_get_dynamic_v1(NULL, &params, &response1);
	zassert_ok(rv, "Got %d", rv);
	zassert_mem_equal(batt, &response1, sizeof(response1));
}

ZTEST_SUITE(host_cmd_battery_info, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);
