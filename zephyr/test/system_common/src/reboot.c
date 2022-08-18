/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test_new.h>

#include "host_command.h"
#include "system.h"

/* This has to be declared here so it's visible in system_reset(), so no point
 * passing it through the fixture parameter of the ztest APIs.
 */
static struct reboot_fixture {
	int reset_called;
	int reset_flags;
	int hibernate_called;
} reboot_fixture;

__override void system_reset(int flags)
{
	reboot_fixture.reset_called++;
	reboot_fixture.reset_flags = flags;
}

__override void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	reboot_fixture.hibernate_called++;
}

static void system_reset_fixture_reset(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&reboot_fixture, 0, sizeof(reboot_fixture));
}

ZTEST_SUITE(console_cmd_reboot, NULL, NULL, system_reset_fixture_reset, NULL,
	    NULL);

ZTEST(console_cmd_reboot, test_reboot_valid)
{
	int ret;
	int i;

	struct {
		char *cmd;
		int expect_called;
		int expect_flags;
	} tests[] = {
		{ "reboot hard", 1,
		  SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_HARD },
		{ "reboot cold", 1,
		  SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_HARD },
		{ "reboot soft", 1, SYSTEM_RESET_MANUALLY_TRIGGERED },
		{ "reboot ap-off", 1,
		  SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_LEAVE_AP_OFF },
		{ "reboot ap-off-in-ro", 1,
		  SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_LEAVE_AP_OFF |
			  SYSTEM_RESET_STAY_IN_RO },
		{ "reboot ro", 1,
		  SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_STAY_IN_RO },
		{ "reboot cancel", 0, 0 },
		{ "reboot preserve", 1,
		  SYSTEM_RESET_MANUALLY_TRIGGERED |
			  SYSTEM_RESET_PRESERVE_FLAGS },
		{ "reboot wait-ext", 1,
		  SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_WAIT_EXT },
	};

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		char *cmd = tests[i].cmd;

		/* Make sure the fixture gets reset before each re-run. */
		system_reset_fixture_reset(NULL);

		ret = shell_execute_cmd(get_ec_shell(), cmd);

		zassert_equal(ret, EC_SUCCESS,
			      "invalid return value for '%s': %d", cmd, ret);
		zassert_equal(reboot_fixture.reset_called,
			      tests[i].expect_called,
			      "Unexpected call count for '%s': %d", cmd,
			      reboot_fixture.reset_called);
		zassert_equal(reboot_fixture.reset_flags, tests[i].expect_flags,
			      "Unexpected flags for '%s': %x", cmd,
			      reboot_fixture.reset_flags);
	}
}

ZTEST(console_cmd_reboot, test_reboot_invalid)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(), "reboot i-am-not-an-argument");

	zassert_equal(ret, EC_ERROR_PARAM1, "invalid return value: %d", ret);
	zassert_equal(reboot_fixture.reset_called, 0,
		      "Unexpected call count: %d", reboot_fixture.reset_called);
}

ZTEST_SUITE(host_cmd_reboot, NULL, NULL, system_reset_fixture_reset, NULL,
	    NULL);

ZTEST(host_cmd_reboot, test_reboot)
{
	int ret;
	int i;
	struct ec_params_reboot_ec p;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_REBOOT_EC, 0, p);
	int reboot_at_shutdown;

	struct {
		uint8_t cmd;
		uint8_t flags;
		int expect_return;
		int expect_reboot_at_shutdown;
		int expect_reset_called;
		int expect_reset_flags;
		int expect_hibernate_called;
	} tests[] = {
		{
			.cmd = EC_REBOOT_CANCEL,
			.flags = 0,
			.expect_return = EC_RES_SUCCESS,
			.expect_reboot_at_shutdown = EC_REBOOT_CANCEL,
			.expect_reset_called = 0,
			.expect_reset_flags = 0,
			.expect_hibernate_called = 0,
		},
		{
			.cmd = EC_REBOOT_COLD,
			.flags = EC_REBOOT_FLAG_SWITCH_RW_SLOT,
			.expect_return = EC_RES_INVALID_PARAM,
			.expect_reboot_at_shutdown = 0,
			.expect_reset_called = 0,
			.expect_reset_flags = 0,
			.expect_hibernate_called = 0,
		},
		{
			.cmd = 0xaa, /* cmd passed unmodified */
			.flags = EC_REBOOT_FLAG_ON_AP_SHUTDOWN,
			.expect_return = EC_RES_SUCCESS,
			.expect_reboot_at_shutdown = 0xaa,
			.expect_reset_called = 0,
			.expect_reset_flags = 0,
			.expect_hibernate_called = 0,
		},
		{
			.cmd = 0x55, /* cmd passed unmodified */
			.flags = EC_REBOOT_FLAG_ON_AP_SHUTDOWN,
			.expect_return = EC_RES_SUCCESS,
			.expect_reboot_at_shutdown = 0x55,
			.expect_reset_called = 0,
			.expect_reset_flags = 0,
			.expect_hibernate_called = 0,
		},
		{
			.cmd = EC_REBOOT_COLD,
			.flags = 0,
			.expect_return = EC_RES_ERROR,
			.expect_reboot_at_shutdown = EC_REBOOT_CANCEL,
			.expect_reset_called = 1,
			.expect_reset_flags = SYSTEM_RESET_HARD,
			.expect_hibernate_called = 0,
		},
		{
			.cmd = EC_REBOOT_HIBERNATE,
			.flags = 0,
			.expect_return = EC_RES_ERROR,
			.expect_reboot_at_shutdown = EC_REBOOT_CANCEL,
			.expect_reset_called = 0,
			.expect_reset_flags = 0,
			.expect_hibernate_called = 1,
		},
		{
			.cmd = EC_REBOOT_COLD_AP_OFF,
			.flags = 0,
			.expect_return = EC_RES_ERROR,
			.expect_reboot_at_shutdown = EC_REBOOT_CANCEL,
			.expect_reset_called = 1,
			.expect_reset_flags = SYSTEM_RESET_HARD |
					      SYSTEM_RESET_LEAVE_AP_OFF,
			.expect_hibernate_called = 0,
		},
		{
			.cmd = 0xff,
			.flags = 0,
			.expect_return = EC_RES_INVALID_PARAM,
			.expect_reboot_at_shutdown = EC_REBOOT_CANCEL,
			.expect_reset_called = 0,
			.expect_reset_flags = 0,
			.expect_hibernate_called = 0,
		},
	};

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		p.cmd = tests[i].cmd;
		p.flags = tests[i].flags;

		/* Make sure the fixture gets reset before each re-run. */
		system_reset_fixture_reset(NULL);

		ret = host_command_process(&args);

		zassert_equal(ret, tests[i].expect_return,
			      "Unexpected return value (%d): %d", i, ret);
		reboot_at_shutdown =
			system_common_get_reset_reboot_at_shutdown();
		zassert_equal(
			reboot_at_shutdown, tests[i].expect_reboot_at_shutdown,
			"Unexpected value for reboot_at_shutdown (%d): %d", i,
			reboot_at_shutdown);
		zassert_equal(reboot_fixture.reset_called,
			      tests[i].expect_reset_called,
			      "Unexpected reset call count (%d): %d", i,
			      reboot_fixture.reset_called);
		zassert_equal(reboot_fixture.reset_flags,
			      tests[i].expect_reset_flags,
			      "Unexpected flags (%d): %x", i,
			      reboot_fixture.reset_flags);
		zassert_equal(reboot_fixture.hibernate_called,
			      tests[i].expect_hibernate_called,
			      "Unexpected hibernate call count (%d): %d", i,
			      reboot_fixture.hibernate_called);
	}
}
