/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "common.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static bool get_ap_reset_stats_should_succeed = true;

/* Mocks */

enum ec_error_list
get_ap_reset_stats(struct ap_reset_log_entry *reset_log_entries,
		   size_t num_reset_log_entries, uint32_t *resets_since_ec_boot)
{
	return get_ap_reset_stats_should_succeed ? EC_SUCCESS : EC_ERROR_INVAL;
}

timestamp_t get_time(void)
{
	timestamp_t fake_time = { .val = 42 * MSEC };
	return fake_time;
}

/* Tests */

test_static int test_host_uptime_info_command_success(void)
{
	int rv;
	struct ec_response_uptime_info resp = { 0 };

	get_ap_reset_stats_should_succeed = true;

	rv = test_send_host_command(EC_CMD_GET_UPTIME_INFO, 0, NULL, 0, &resp,
				    sizeof(resp));

	TEST_ASSERT(rv == EC_RES_SUCCESS);
	TEST_ASSERT(resp.time_since_ec_boot_ms == 42);

	return EC_RES_SUCCESS;
}

test_static int test_host_uptime_info_command_failure(void)
{
	int rv;
	struct ec_response_uptime_info resp = { 0 };

	get_ap_reset_stats_should_succeed = false;

	rv = test_send_host_command(EC_CMD_GET_UPTIME_INFO, 0, NULL, 0, &resp,
				    sizeof(resp));

	TEST_ASSERT(rv == EC_RES_ERROR);

	return EC_RES_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_host_uptime_info_command_success);
	RUN_TEST(test_host_uptime_info_command_failure);

	test_print_result();
}
