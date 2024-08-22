/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "debug.h"
#include "flash.h"
#include "multistep_test.h"
#include "system.h"
#include "write_protect.h"

#include <zephyr/ztest.h>

#include <cmsis_core.h>
#include <strings.h>

ZTEST_SUITE(system_is_locked_wp_off, NULL, NULL, NULL, NULL, NULL);

static bool write_protect_enabled;

static void test_write_protect(void)
{
	zassert_equal(write_protect_is_asserted(), write_protect_enabled);
}

static void test_verify_system_is_locked(void)
{
	if (!write_protect_is_asserted() ||
	    (~crec_flash_get_protect() & EC_FLASH_PROTECT_RO_NOW))
		zassert_equal(system_is_locked(), 0);
	else
		zassert_equal(system_is_locked(), 1);
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
static void test_ensure_no_debugger_detected(void)
{
	zassert_false(debugger_is_connected());
	zassert_false(debugger_was_connected());
}

static void test_ro_protection_enabled(void)
{
	zassert_true(crec_flash_get_protect() & EC_FLASH_PROTECT_RO_NOW);
}

/*
 * Step 1: Check if reported write protect and system_is_locked()
 * output is correct. Since RO protection is not enabled at this point
 * we expect system_is_locked() to return 0. If write protect is
 * enabled then attempt to enable RO protection.
 */
static void test_step1(void)
{
	test_write_protect();
	test_verify_system_is_locked();

	if (write_protect_enabled) {
		test_ensure_no_debugger_detected();
		ccprintf("Request RO protection at boot\n");
		cflush();
		crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
				       EC_FLASH_PROTECT_RO_AT_BOOT);

		system_reset(SYSTEM_RESET_HARD);
	}
}

/*
 * Step 2: Check if hardware write protect is enabled, RO protection
 * is enabled and system_is_locked() returns 1.
 */
static void test_step2(void)
{
	/* Expect hardware write protect to be enabled */
	write_protect_enabled = true;
	test_write_protect();
	test_ro_protection_enabled();
	test_verify_system_is_locked();
}

static void test_pre_check(void)
{
	if (IS_ENABLED(CONFIG_SYSTEM_UNLOCKED)) {
		ccprintf("Please disable CONFIG_SYSTEM_UNLOCKED before "
			 "running this test\n");
		zassert_unreachable();
	}
}

ZTEST(system_is_locked_wp_off, test_system_is_locked)
{
	if (IS_ENABLED(CONFIG_WP_ALWAYS)) {
		ccprintf("Hardware write protect always enabled. "
			 "Please disable CONFIG_WP_ALWAYS before "
			 "running this test\n");
		zassert_unreachable();
	}
	test_pre_check();
	write_protect_enabled = false;

	test_step1();
}

static void test_step1_wp_on(void)
{
	test_pre_check();
	write_protect_enabled = true;

	test_step1();
}

static void test_step2_wp_on(void)
{
	test_pre_check();
	write_protect_enabled = true;

	test_step2();
}

static void (*test_steps[])(void) = { test_step1_wp_on, test_step2_wp_on };

MULTISTEP_TEST(system_is_locked_wp_on, test_steps)
