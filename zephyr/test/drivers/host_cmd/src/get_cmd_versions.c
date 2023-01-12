/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_USER(hc_get_cmd_versions, test_v0__both_versions)
{
	struct ec_params_get_cmd_versions params = {
		.cmd = EC_CMD_GET_CMD_VERSIONS,
	};
	struct ec_response_get_cmd_versions response;
	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_get_cmd_versions(&args, &params, &response));
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(response.version_mask, EC_VER_MASK(0) | EC_VER_MASK(1));
}

ZTEST_USER(hc_get_cmd_versions, test_v1__only_v0)
{
	struct ec_params_get_cmd_versions_v1 params = {
		.cmd = EC_CMD_HELLO,
	};
	struct ec_response_get_cmd_versions response;
	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_get_cmd_versions_v1(&args, &params, &response));
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(response.version_mask, EC_VER_MASK(0));
}

ZTEST_USER(hc_get_cmd_versions, test_v1__bad_cmd)
{
	struct ec_params_get_cmd_versions_v1 params = {
		/* Non-existent host-command */
		.cmd = UINT16_MAX,
	};
	struct ec_response_get_cmd_versions response;

	zassert_equal(ec_cmd_get_cmd_versions_v1(NULL, &params, &response),
		      EC_RES_INVALID_PARAM);
}

ZTEST_SUITE(hc_get_cmd_versions, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
