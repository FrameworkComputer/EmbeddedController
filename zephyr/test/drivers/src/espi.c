/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>

#include "ec_commands.h"
#include "host_command.h"

static void test_host_command_get_protocol_info(void)
{
	struct ec_response_get_protocol_info response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, 0, response);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.protocol_versions, BIT(3), NULL);
	zassert_equal(response.max_request_packet_size, EC_LPC_HOST_PACKET_SIZE,
		      NULL);
	zassert_equal(response.max_response_packet_size,
		      EC_LPC_HOST_PACKET_SIZE, NULL);
	zassert_equal(response.flags, 0, NULL);
}

void test_suite_espi(void)
{
	ztest_test_suite(espi,
			 ztest_user_unit_test(
				 test_host_command_get_protocol_info));
	ztest_run_test_suite(espi);
}
