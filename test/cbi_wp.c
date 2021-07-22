/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test CBI EEPROM WP
 */

#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "gpio.h"
#include "system.h"
#include "test_util.h"
#include "util.h"

static int system_locked;

static void test_setup(void)
{
	/* Make sure that write protect is disabled */
	gpio_set_level(GPIO_WP, 0);
	gpio_set_level(GPIO_EC_CBI_WP, 0);
	system_locked = 0;
}

static void test_teardown(void)
{
}

int system_is_locked(void)
{
	return system_locked;
}

DECLARE_EC_TEST(test_wp)
{
	int cbi_wp;

	cbi_wp = gpio_get_level(GPIO_EC_CBI_WP);
	zassert_equal(cbi_wp, 0, NULL);

	cbi_latch_eeprom_wp();
	cbi_wp = gpio_get_level(GPIO_EC_CBI_WP);
	zassert_equal(cbi_wp, 1, NULL);

	return EC_SUCCESS;
}

TEST_SUITE(test_suite_cbi_wp)
{
	ztest_test_suite(test_cbi_wp,
			 ztest_unit_test_setup_teardown(test_wp,
							test_setup,
							test_teardown));
	ztest_run_test_suite(test_cbi_wp);
}
