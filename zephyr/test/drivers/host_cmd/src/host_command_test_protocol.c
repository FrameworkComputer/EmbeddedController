/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_USER(hc_test_protocol, test_echo_max_buffer_size)
{
	struct ec_params_test_protocol params = {
		/* Actual return length will only be sizeof(response) */
		.ret_len = UINT32_MAX,
		.buf = { 0 },
		.ec_result = EC_SUCCESS,
	};
	struct ec_response_test_protocol response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_TEST_PROTOCOL, 0, response, params);

	/* Set first and last bytes of input array */
	params.buf[0] = 0x1;
	params.buf[ARRAY_SIZE(params.buf) - 1] = 0x2;

	zassert_ok(host_command_process(&args));
	zassert_equal(args.response_size, sizeof(response));

	/* Check contents are echoed back in response */
	zassert_mem_equal(params.buf, response.buf, sizeof(params.buf));
}

ZTEST_USER(hc_test_protocol, test_echo_min_buffer_size_failing_command)
{
	struct ec_params_test_protocol params = {
		/* Don't want to have anything echoed back to us */
		.ret_len = 0,
		.buf = { 0 },
		.ec_result = EC_ERROR_TRY_AGAIN,
	};
	struct ec_response_test_protocol response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_TEST_PROTOCOL, 0, response, params);

	/*
	 * Set first and last bytes of input array, neither will be written to
	 * response
	 */
	params.buf[0] = 0x1;
	params.buf[ARRAY_SIZE(params.buf) - 1] = 0x2;

	zassert_equal(host_command_process(&args), params.ec_result);
	zassert_equal(args.response_size, params.ret_len);

	/* Check contents were never echoed back as intended */
	zassert_not_equal(response.buf[0], params.buf[0]);
	zassert_not_equal(response.buf[ARRAY_SIZE(response.buf) - 1],
			  params.buf[ARRAY_SIZE(params.buf) - 1]);
}

ZTEST_SUITE(hc_test_protocol, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
