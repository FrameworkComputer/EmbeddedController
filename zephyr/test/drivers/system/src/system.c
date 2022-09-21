/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "host_command.h"
#include "system.h"
#include "test/drivers/test_state.h"

/* System Host Commands */

ZTEST_USER(system, test_hostcmd_sysinfo)
{
	struct ec_response_sysinfo response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_SYSINFO, 0, response);

	/* Simply issue the command and get the results */
	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.reset_flags, 0, "response.reset_flags = %d",
		      response.reset_flags);
	zassert_equal(response.current_image, EC_IMAGE_RO,
		      "response.current_image = %d", response.current_image);
	zassert_equal(response.flags, 0, "response.flags = %d", response.flags);
}

ZTEST_USER(system, test_hostcmd_board_version)
{
	struct ec_response_board_version response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_BOARD_VERSION, 0, response);

	/* Get the board version, which is default 0. */
	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.board_version, 0, "response.board_version = %d",
		      response.board_version);
}

/* System Function Testing */

static void system_flags_before_after(void *data)
{
	ARG_UNUSED(data);
	system_clear_reset_flags(-1);
}

ZTEST(system_save_flags, test_system_encode_save_flags)
{
	int flags_to_save = 0;
	uint32_t saved_flags = 0;
	int arbitrary_reset_flags = 1;

	/* Save all possible flags */
	flags_to_save = -1;

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(arbitrary_reset_flags);

	system_encode_save_flags(flags_to_save, &saved_flags);

	/* Verify all non-mutually exclusive flags */
	zassert_equal(1, saved_flags & system_get_reset_flags(), NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_AP_OFF, NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_STAY_IN_RO, NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_AP_WATCHDOG, NULL);
}

ZTEST(system_save_flags,
      test_system_encode_save_flags_mutually_exclusive_reset_flags)
{
	int flags_to_save = 0;
	uint32_t saved_flags = 0;

	/* Verify reset hard takes precedence over hibernate/soft */
	flags_to_save = SYSTEM_RESET_HARD | SYSTEM_RESET_HIBERNATE;

	system_encode_save_flags(flags_to_save, &saved_flags);

	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_HARD, NULL);
	zassert_equal(0, saved_flags & EC_RESET_FLAG_HIBERNATE, NULL);
	zassert_equal(0, saved_flags & EC_RESET_FLAG_SOFT, NULL);

	/* Verify reset hibernate takes precedence over soft */
	flags_to_save = SYSTEM_RESET_HIBERNATE;

	system_encode_save_flags(flags_to_save, &saved_flags);

	zassert_equal(0, saved_flags & EC_RESET_FLAG_HARD, NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_HIBERNATE, NULL);
	zassert_equal(0, saved_flags & EC_RESET_FLAG_SOFT, NULL);

	/* Verify reset soft is always saved given no other flags */
	flags_to_save = 0;

	system_encode_save_flags(flags_to_save, &saved_flags);

	zassert_equal(0, saved_flags & EC_RESET_FLAG_HARD, NULL);
	zassert_equal(0, saved_flags & EC_RESET_FLAG_HIBERNATE, NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_SOFT, NULL);
}

ZTEST_SUITE(system, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST_SUITE(system_save_flags, drivers_predicate_post_main, NULL,
	    system_flags_before_after, system_flags_before_after, NULL);
