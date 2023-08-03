/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "debug.h"
#include "flash.h"
#include "string.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "write_protect.h"

static bool write_protect_enabled;

test_static int test_write_protect(void)
{
	TEST_EQ(write_protect_is_asserted(), write_protect_enabled, "%d");

	return EC_SUCCESS;
}

/*
 * This is more of a pre-condition, since further tests will fail in
 * non-obvious ways if the STM32 chip thinks a debugger is or was attached
 * once RDP is enabled. This is part of the stm32 flash RDP security feature.
 *
 * This debugger state will persist even after the debugger is
 * disconnected. The only way to reset this state is to physically reset or
 * power cycle the MCU.

 * These tests can only help predict what the stm32 flash controller might
 * think. We can't actually test the state it uses to determine if a debugger
 * was/is attached.
 */
test_static int test_ensure_no_debugger_detected(void)
{
	TEST_EQ(debugger_is_connected(), false, "%d");
	TEST_EQ(debugger_was_connected(), false, "%d");

	return EC_SUCCESS;
}

test_static int test_ro_protection_enabled(void)
{
	TEST_BITS_SET(crec_flash_get_protect(), EC_FLASH_PROTECT_RO_NOW);

	return EC_SUCCESS;
}

test_static int test_system_is_locked(void)
{
	if (!write_protect_is_asserted() ||
	    (~crec_flash_get_protect() & EC_FLASH_PROTECT_RO_NOW))
		TEST_EQ(system_is_locked(), 0, "%d");
	else
		TEST_EQ(system_is_locked(), 1, "%d");

	return EC_SUCCESS;
}

static void print_usage(void)
{
	ccprintf("usage: runtest [wp_on|wp_off]\n");
}

void test_run_step(uint32_t state)
{
	/*
	 * Step 1: Check if reported write protect and system_is_locked()
	 * output is correct. Since RO protection is not enabled at this point
	 * we expect system_is_locked() to return 0. If write protect is
	 * enabled then attempt to enable RO protection.
	 */
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1)) {
		RUN_TEST(test_write_protect);
		RUN_TEST(test_system_is_locked);

		if (test_get_error_count())
			test_reboot_to_next_step(TEST_STATE_FAILED);
		else if (write_protect_enabled) {
			RUN_TEST(test_ensure_no_debugger_detected);
			ccprintf("Request RO protection at boot\n");
			cflush();
			crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
					       EC_FLASH_PROTECT_RO_AT_BOOT);
			test_reboot_to_next_step(TEST_STATE_STEP_2);
		} else {
			/* Write protect is disabled, nothing else to do */
			test_reboot_to_next_step(TEST_STATE_PASSED);
		}
	}
	/*
	 * Step 2: Check if hardware write protect is enabled, RO protection
	 * is enabled and system_is_locked() returns 1.
	 */
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		/* Expect hardware write protect to be enabled */
		write_protect_enabled = true;
		RUN_TEST(test_write_protect);
		RUN_TEST(test_ro_protection_enabled);
		RUN_TEST(test_system_is_locked);
		if (test_get_error_count())
			test_reboot_to_next_step(TEST_STATE_FAILED);
		else
			test_reboot_to_next_step(TEST_STATE_PASSED);
	}
}

int task_test(void *unused)
{
	test_run_multistep();
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	if (IS_ENABLED(CONFIG_SYSTEM_UNLOCKED)) {
		ccprintf("Please disable CONFIG_SYSTEM_UNLOCKED before "
			 "running this test\n");
		test_fail();
		return;
	}

	if (argc < 2) {
		print_usage();
		test_fail();
		return;
	}

	if (strncmp(argv[1], "wp_on", 5) == 0)
		write_protect_enabled = true;
	else if (strncmp(argv[1], "wp_off", 6) == 0) {
		write_protect_enabled = false;
		if (IS_ENABLED(CONFIG_WP_ALWAYS)) {
			ccprintf("Hardware write protect always enabled. "
				 "Please disable CONFIG_WP_ALWAYS before "
				 "running this test\n");
			test_fail();
			return;
		}
	} else {
		print_usage();
		test_fail();
		return;
	}

	msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
