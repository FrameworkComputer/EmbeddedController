/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include "ec_app_main.h"
#include "hooks.h"

static void test_init_reset_log(void)
{
#ifdef CONFIG_CMD_AP_RESET_LOG
	zassert_unreachable("TODO: Implement this test.");
#else
	ztest_test_skip();
#endif
}

static void test_lpc_init_mask(void)
{
#ifdef CONFIG_HOSTCMD_X86
	zassert_unreachable("TODO: Implement this test.");
#else
	ztest_test_skip();
#endif
}

static void test_keyboard_scan_init(void)
{
#ifdef HAS_TASK_KEYSCAN
	zassert_unreachable("TODO: Implement this test.");
#else
	ztest_test_skip();
#endif
}

static void test_button_init(void)
{
#if defined(CONFIG_DEDICATED_RECOVERY_BUTTON) || defined(CONFIG_VOLUME_BUTTONS)
	zassert_unreachable("TODO: Implement this test.");
#else
	ztest_test_skip();
#endif
}

static void test_setup_espi(void)
{
#ifdef CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI
	zassert_unreachable("TODO: Implement this test.");
#else
	ztest_test_skip();
#endif
}

static void test_watchdog_init(void)
{
#ifdef CONFIG_PLATFORM_EC_WATCHDOG
	zassert_unreachable("TODO: Implement this test.");
#else
	ztest_test_skip();
#endif
}

static void test_vboot_main(void)
{
#ifdef CONFIG_PLATFORM_EC_VBOOT_EFS2
	zassert_unreachable("TODO: Implement this test.");
#else
	ztest_test_skip();
#endif
}

static void test_start_ec_tasks(void)
{
#ifdef CONFIG_SHIMMED_TASKS
	zassert_unreachable("TODO: Implement this test.");
#else
	ztest_test_skip();
#endif
}

void test_main(void)
{
	ztest_test_suite(ec_app_tests, ztest_unit_test(test_init_reset_log),
			 ztest_unit_test(test_lpc_init_mask),
			 ztest_unit_test(test_keyboard_scan_init),
			 ztest_unit_test(test_button_init),
			 ztest_unit_test(test_setup_espi),
			 ztest_unit_test(test_watchdog_init),
			 ztest_unit_test(test_vboot_main),
			 ztest_unit_test(test_start_ec_tasks));

	ztest_run_test_suite(ec_app_tests);
}
