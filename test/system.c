/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test system_common.
 */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define TEST_STATE_STEP_2 (1 << 0)
#define TEST_STATE_FAIL (1 << 1)

static int test_reboot_on_shutdown(void)
{
	struct ec_params_reboot_ec params;

	/* Fails if the system reboots unexpectedly */
	system_set_scratchpad(TEST_STATE_FAIL);

	test_chipset_on();
	msleep(30);

	params.cmd = EC_REBOOT_COLD;
	params.flags = EC_REBOOT_FLAG_ON_AP_SHUTDOWN;

	TEST_EQ(test_send_host_command(EC_CMD_REBOOT_EC, 0, &params,
				       sizeof(params), NULL, 0),
		EC_SUCCESS, "%d");

	system_set_scratchpad(TEST_STATE_STEP_2);
	test_chipset_off();
	msleep(30);

	/* Shouldn't reach here */
	return EC_ERROR_UNKNOWN;
}

static int test_cancel_reboot(void)
{
	struct ec_params_reboot_ec params;

	/* Fails if the system reboots unexpectedly */
	system_set_scratchpad(TEST_STATE_FAIL);

	test_chipset_on();
	msleep(30);

	params.cmd = EC_REBOOT_COLD;
	params.flags = EC_REBOOT_FLAG_ON_AP_SHUTDOWN;

	TEST_EQ(test_send_host_command(EC_CMD_REBOOT_EC, 0, &params,
				       sizeof(params), NULL, 0),
		EC_SUCCESS, "%d");

	params.cmd = EC_REBOOT_CANCEL;
	params.flags = 0;

	TEST_EQ(test_send_host_command(EC_CMD_REBOOT_EC, 0, &params,
				       sizeof(params), NULL, 0),
		EC_SUCCESS, "%d");

	test_chipset_off();
	msleep(30);

	return EC_SUCCESS;
}

static void run_test_step1(void)
{
	if (test_reboot_on_shutdown() != EC_SUCCESS)
		test_fail();
}

static void run_test_step2(void)
{
	if (test_cancel_reboot() != EC_SUCCESS)
		test_fail();

	system_set_scratchpad(0);
	test_pass();
}

static void fail_and_clean_up(void)
{
	system_set_scratchpad(0);
	test_fail();
}

void run_test(int argc, const char **argv)
{
	uint32_t state = 0;

	/* The return value isn't checked here on purpose. The scratchpad file
	 * may exist from a previous run or it may be in a clean state. A
	 * previous run of this test should reset the scratchpad value to 0
	 * regardless of the final result.
	 */
	system_get_scratchpad(&state);

	test_reset();

	if (state == 0)
		run_test_step1();
	else if (state & TEST_STATE_STEP_2)
		run_test_step2();
	else if (state & TEST_STATE_FAIL)
		fail_and_clean_up();
}
