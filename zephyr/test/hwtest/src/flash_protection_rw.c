/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "multistep_test.h"
#include "system.h"
#include "write_protect.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_flash_protection_rw);

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
}

static void (*test_steps[])(void) = {
	test_set_ro_at_boot,
	test_check_all_now_in_rw,
};

MULTISTEP_TEST(flash_protection_rw, test_steps);
