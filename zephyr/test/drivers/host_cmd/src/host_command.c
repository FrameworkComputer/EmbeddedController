/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST(host_cmd_host_commands, test_get_command_versions__v1)
{
	struct ec_response_get_cmd_versions response;
	struct ec_params_get_cmd_versions_v1 params = {
		.cmd = EC_CMD_GET_CMD_VERSIONS
	};
	int rv;

	rv = ec_cmd_get_cmd_versions_v1(NULL, &params, &response);

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(EC_VER_MASK(0) | EC_VER_MASK(1), response.version_mask);
}

ZTEST(host_cmd_host_commands, test_get_command_versions__invalid_cmd)
{
	struct ec_response_get_cmd_versions response;
	struct ec_params_get_cmd_versions_v1 params = {
		/* Host command doesn't exist */
		.cmd = UINT16_MAX,
	};
	int rv;

	rv = ec_cmd_get_cmd_versions_v1(NULL, &params, &response);

	zassert_equal(EC_RES_INVALID_PARAM, rv, "Got %d", rv);
}

ZTEST(host_cmd_host_commands, test_get_comms_status)
{
	struct ec_response_get_comms_status response;
	int rv;

	rv = ec_cmd_get_comms_status(NULL, &response);

	zassert_ok(rv, "Got %d", rv);

	/* Unit test host commands are processed synchronously, so always expect
	 * the EC to be not busy processing another.
	 */
	zassert_false(response.flags);
}

ZTEST(host_cmd_host_commands, test_resend_response)
{
	struct host_cmd_handler_args args =
		(struct host_cmd_handler_args)BUILD_HOST_COMMAND_SIMPLE(
			EC_CMD_RESEND_RESPONSE, 0);
	int rv;

	rv = host_command_process(&args);
	zassert_ok(rv);

	/* The way we trigger host commands in tests doesn't cause results to
	 * get saved (it happens outside of host_command_process), so we cannot
	 * verify the resent response itself.
	 *
	 * TODO: test at least one host command through the ESPI interface.
	 */
}

ZTEST(host_cmd_host_commands, test_get_proto_version)
{
	struct ec_response_proto_version response;
	int rv;

	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_PROTO_VERSION, 0, response);

	rv = host_command_process(&args);

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(EC_PROTO_VERSION, response.version);
}

ZTEST(host_cmd_host_commands, test_hello)
{
	struct ec_response_hello response;
	struct ec_params_hello params;
	int rv;
	uint32_t params_to_test[] = { 0x0, 0xaaaaaaaa, 0xffffffff };
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HELLO, 0, response, params);

	for (int i = 0; i < ARRAY_SIZE(params_to_test); i++) {
		params.in_data = params_to_test[i];

		rv = host_command_process(&args);

		zassert_ok(rv, "Got %d, in_data: %x", rv, params_to_test[i]);
		zassert_equal(params_to_test[i] + 0x01020304, response.out_data,
			      "in_data: %x", params_to_test[i]);
	}
}

ZTEST_SUITE(host_cmd_host_commands, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);
