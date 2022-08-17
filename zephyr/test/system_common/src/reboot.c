/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test_new.h>

#include "system.h"

/* This has to be declared here so it's visible in system_reset(), so no point
 * passing it through the fixture parameter of the ztest APIs.
 */
static struct console_cmd_reboot_fixture {
	int reset_called;
	int reset_flags;
} reboot_fixture;

static void console_cmd_reboot_before(void *fixture)
{
	reboot_fixture.reset_called = 0;
	reboot_fixture.reset_flags = 0;
}

__override void system_reset(int flags)
{
	reboot_fixture.reset_called++;
	reboot_fixture.reset_flags = flags;
}

ZTEST_SUITE(console_cmd_reboot, NULL, NULL, console_cmd_reboot_before, NULL,
	    NULL);

ZTEST(console_cmd_reboot, test_reboot_valid)
{
	int ret;
	int i;

	struct {
		char *cmd;
		int called_expect;
		int flags_expect;
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
		console_cmd_reboot_before(NULL);

		ret = shell_execute_cmd(get_ec_shell(), cmd);

		zassert_equal(ret, EC_SUCCESS,
			      "invalid return value for '%s': %d", cmd, ret);
		zassert_equal(reboot_fixture.reset_called,
			      tests[i].called_expect,
			      "Unexpected call count for '%s': %d", cmd,
			      reboot_fixture.reset_called);
		zassert_equal(reboot_fixture.reset_flags, tests[i].flags_expect,
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
