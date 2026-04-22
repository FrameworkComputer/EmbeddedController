/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "system.h"
#include "write_protect.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_test.h>

LOG_MODULE_REGISTER(test_flash_protection);

static void test_set_ro_at_boot(void)
{
	int rv;
	bool wp = write_protect_is_asserted();

	zassert_true(wp, "WP must be enabled");
	LOG_INF("WP enabled, setting RO_AT_BOOT");
	rv = crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
				    EC_FLASH_PROTECT_RO_AT_BOOT);
	zassert_equal(rv, EC_SUCCESS);
	LOG_INF("RO_AT_BOOT set, rebooting");
	cflush();
	system_reset(SYSTEM_RESET_HARD);
}

static void test_check_all_now_in_rw(void)
{
	zassert_equal(system_get_image_copy(), EC_IMAGE_RW,
		      "Not in RW after reboot");
	LOG_INF("Rebooted to RW");
	zassert_equal(crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW,
		      EC_FLASH_PROTECT_ALL_NOW, "ALL_NOW not set after reboot");
	LOG_INF("ALL_NOW set in RW, rebooting to RO");
	cflush();
	system_reset(SYSTEM_RESET_STAY_IN_RO);
}

static void test_check_all_now_in_ro(void)
{
	zassert_equal(system_get_image_copy(), EC_IMAGE_RO,
		      "Not in RO after reboot");
	LOG_INF("Rebooted to RO");
	zassert_not_equal(crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW,
			  EC_FLASH_PROTECT_ALL_NOW,
			  "ALL_NOW set after reboot to RO");
	LOG_INF("ALL_NOW not set in RO");
}

static void (*test_steps[])(void) = { test_set_ro_at_boot,
				      test_check_all_now_in_rw,
				      test_check_all_now_in_ro };

/*
 * Use custom method of multistep testing, because steps have to be run in a
 * different image - 2 steps in RW, one step in RO. This is not possible with
 * the common pattern.
 */

/* Use a unique step 0 value. */
#define STEP_ZERO ((UINT16_MAX / 2) + 1)
#define TEST_STEP(step) (step - STEP_ZERO)

static struct k_work multistep_test_work;
static void multistep_test_handler(struct k_work *work)
{
	uint32_t step = 0;

	system_get_scratchpad(&step);
	if ((step > STEP_ZERO) && (step < STEP_ZERO + ARRAY_SIZE(test_steps))) {
		/* Run only "test_check_all_now_in_ro" step in RO. */
		if ((system_get_image_copy() == EC_IMAGE_RO) &&
		    (test_steps[TEST_STEP(step)] != test_check_all_now_in_ro)) {
			return;
		}
		ztest_run_test_suite(flash_protection, false, 1, 1, NULL);
	}
}

static int multistep_test_init(void)
{
	k_work_init(&multistep_test_work, multistep_test_handler);

	/* Check if the test has to be run after reboot */
	k_work_submit(&multistep_test_work);

	return 0;
}
SYS_INIT(multistep_test_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

static void *flash_protection_setup(void)
{
	uint32_t step = 0;
	int ret = 0;

	ret = system_get_scratchpad(&step);
	if ((step < STEP_ZERO) ||
	    (step >= (STEP_ZERO + ARRAY_SIZE(test_steps)))) {
		system_set_scratchpad(STEP_ZERO);
	}
	zassert_equal(ret, 0);

	return NULL;
}

static void flash_protection_teardown(void *fixture)
{
	system_set_scratchpad(0);
}

ZTEST(flash_protection, test_flash_protection_logic)
{
	uint32_t step = 0;
	int ret = 0;

	ret = system_get_scratchpad(&step);
	zassert_equal(ret, 0);
	ret = system_set_scratchpad(step + 1);

	test_steps[TEST_STEP(step)]();
}
ZTEST_SUITE(flash_protection, NULL, flash_protection_setup, NULL, NULL,
	    flash_protection_teardown);
