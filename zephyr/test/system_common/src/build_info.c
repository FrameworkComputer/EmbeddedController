/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

ZTEST_SUITE(host_cmd_get_build_info, NULL, NULL, NULL, NULL, NULL);

FAKE_VALUE_FUNC(const char *, system_get_build_info);

ZTEST(host_cmd_get_build_info, test_get_build_info)
{
	int ret;
	char resp[1024];
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_GET_BUILD_INFO, 0, resp);

	RESET_FAKE(system_get_build_info);
	system_get_build_info_fake.return_val = "i-am-a-version";

	ret = host_command_process(&args);

	zassert_equal(ret, EC_SUCCESS, "Unexpected return value: %d", ret);
	zassert_equal(strcmp(resp, "i-am-a-version"), 0,
		      "Unexpected response: %s", resp);
	zassert_equal(system_get_build_info_fake.call_count, 1,
		      "Unexpected call count: %d",
		      system_get_build_info_fake.call_count);
}

ZTEST(host_cmd_get_build_info, test_get_build_info_truncated)
{
	int ret;
	char resp[8];
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_GET_BUILD_INFO, 0, resp);

	RESET_FAKE(system_get_build_info);
	system_get_build_info_fake.return_val = "i-am-a-long-version";

	ret = host_command_process(&args);

	zassert_equal(ret, EC_SUCCESS, "Unexpected return value: %d", ret);
	zassert_equal(strcmp(resp, "i-am-a-"), 0, "Unexpected response: %s",
		      resp);
	zassert_equal(system_get_build_info_fake.call_count, 1,
		      "Unexpected call count: %d",
		      system_get_build_info_fake.call_count);
}
