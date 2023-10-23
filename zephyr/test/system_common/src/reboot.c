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

FAKE_VOID_FUNC(system_reset, int);
FAKE_VOID_FUNC(system_hibernate, uint32_t, uint32_t);

ZTEST_SUITE(console_cmd_reboot, NULL, NULL, NULL, NULL, NULL);

ZTEST(console_cmd_reboot, test_reboot_valid)
{
	int ret;
	int i;

	struct {
		char *cmd;
		int expect_called;
		int expect_flags;
	} tests[] = {
		{
			.cmd = "reboot hard",
			.expect_called = 1,
			.expect_flags = SYSTEM_RESET_MANUALLY_TRIGGERED |
					SYSTEM_RESET_HARD,
		},
		{
			.cmd = "reboot cold",
			.expect_called = 1,
			.expect_flags = SYSTEM_RESET_MANUALLY_TRIGGERED |
					SYSTEM_RESET_HARD,
		},
		{
			.cmd = "reboot soft",
			.expect_called = 1,
			.expect_flags = SYSTEM_RESET_MANUALLY_TRIGGERED,
		},
		{
			.cmd = "reboot ap-off",
			.expect_called = 1,
			.expect_flags = SYSTEM_RESET_MANUALLY_TRIGGERED |
					SYSTEM_RESET_LEAVE_AP_OFF,
		},
		{
			.cmd = "reboot ap-off-in-ro",
			.expect_called = 1,
			.expect_flags = SYSTEM_RESET_MANUALLY_TRIGGERED |
					SYSTEM_RESET_LEAVE_AP_OFF |
					SYSTEM_RESET_STAY_IN_RO,
		},
		{
			.cmd = "reboot ro",
			.expect_called = 1,
			.expect_flags = SYSTEM_RESET_MANUALLY_TRIGGERED |
					SYSTEM_RESET_STAY_IN_RO,
		},
		{
			.cmd = "reboot cancel",
			.expect_called = 0,
			.expect_flags = 0,
		},
		{
			.cmd = "reboot preserve",
			.expect_called = 1,
			.expect_flags = SYSTEM_RESET_MANUALLY_TRIGGERED |
					SYSTEM_RESET_PRESERVE_FLAGS,
		},
		{
			.cmd = "reboot wait-ext",
			.expect_called = 1,
			.expect_flags = SYSTEM_RESET_MANUALLY_TRIGGERED |
					SYSTEM_RESET_WAIT_EXT,
		},
	};

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		char *cmd = tests[i].cmd;

		RESET_FAKE(system_reset);
		RESET_FAKE(system_hibernate);

		ret = shell_execute_cmd(get_ec_shell(), cmd);

		zassert_equal(ret, EC_SUCCESS,
			      "Unexpected return value for '%s': %d", cmd, ret);
		zassert_equal(system_reset_fake.call_count,
			      tests[i].expect_called,
			      "Unexpected call count for '%s': %d", cmd,
			      system_reset_fake.call_count);
		zassert_equal(system_reset_fake.arg0_history[0],
			      tests[i].expect_flags,
			      "Unexpected flags for '%s': %x", cmd,
			      system_reset_fake.arg0_history[0]);
	}
}

ZTEST(console_cmd_reboot, test_reboot_invalid)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(), "reboot i-am-not-an-argument");

	zassert_equal(ret, EC_ERROR_PARAM1, "invalid return value: %d", ret);
	zassert_equal(system_reset_fake.call_count, 0,
		      "Unexpected call count: %d",
		      system_reset_fake.call_count);
}

ZTEST_SUITE(host_cmd_reboot, NULL, NULL, NULL, NULL, NULL);

ZTEST(host_cmd_reboot, test_reboot)
{
	int ret;
	int i;
	struct ec_params_reboot_ec p;
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

		RESET_FAKE(system_reset);
		RESET_FAKE(system_hibernate);

		ret = ec_cmd_reboot_ec(NULL, &p);

		zassert_equal(ret, tests[i].expect_return,
			      "Unexpected return value (%d): %d", i, ret);
		reboot_at_shutdown =
			system_common_get_reset_reboot_at_shutdown();
		zassert_equal(
			reboot_at_shutdown, tests[i].expect_reboot_at_shutdown,
			"Unexpected value for reboot_at_shutdown (%d): %d", i,
			reboot_at_shutdown);
		zassert_equal(system_reset_fake.call_count,
			      tests[i].expect_reset_called,
			      "Unexpected reset call count (%d): %d", i,
			      system_reset_fake.call_count);
		zassert_equal(system_reset_fake.arg0_history[0],
			      tests[i].expect_reset_flags,
			      "Unexpected flags (%d): %x", i,
			      system_reset_fake.arg0_history[0]);
		zassert_equal(system_hibernate_fake.call_count,
			      tests[i].expect_hibernate_called,
			      "Unexpected hibernate call count (%d): %d", i,
			      system_hibernate_fake.call_count);
	}
}

ZTEST_SUITE(console_cmd_hibernate, NULL, NULL, NULL, NULL, NULL);

int chipset_in_state(int state_mask)
{
	return 0;
}

ZTEST(console_cmd_hibernate, test_hibernate_default)
{
	int ret;

	RESET_FAKE(system_hibernate);

	ret = shell_execute_cmd(get_ec_shell(), "hibernate");

	zassert_equal(ret, EC_SUCCESS, "Unexpected return value: %d", ret);
	zassert_equal(system_hibernate_fake.call_count, 1,
		      "Unexpected hibernate call count: %d",
		      system_hibernate_fake.call_count);
	zassert_equal(system_hibernate_fake.arg0_history[0], 0,
		      "Unexpected hibernate_secondst: %d",
		      system_hibernate_fake.arg0_history[0]);
	zassert_equal(system_hibernate_fake.arg1_history[0], 0,
		      "Unexpected hibernate_secondst: %d",
		      system_hibernate_fake.arg1_history[0]);
}

ZTEST(console_cmd_hibernate, test_hibernate_args)
{
	int ret;

	RESET_FAKE(system_hibernate);

	ret = shell_execute_cmd(get_ec_shell(), "hibernate 123 456");

	zassert_equal(ret, EC_SUCCESS, "Unexpected return value: %d", ret);
	zassert_equal(system_hibernate_fake.call_count, 1,
		      "Unexpected hibernate call count: %d",
		      system_hibernate_fake.call_count);
	zassert_equal(system_hibernate_fake.arg0_history[0], 123,
		      "Unexpected hibernate_secondst: %d",
		      system_hibernate_fake.arg0_history[0]);
	zassert_equal(system_hibernate_fake.arg1_history[0], 456,
		      "Unexpected hibernate_secondst: %d",
		      system_hibernate_fake.arg1_history[0]);
}
