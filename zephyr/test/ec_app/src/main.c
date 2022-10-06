/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test_new.h>
#include <zephyr/shell/shell_dummy.h>

#include "ec_app_main.h"
#include "hooks.h"
#include "task.h"

#ifdef CONFIG_CMD_AP_RESET_LOG
ZTEST(ec_app_tests, test_init_reset_log)
{
	zassert_unreachable("TODO: Implement this test.");
}
#endif

#ifdef CONFIG_HOSTCMD_X86
ZTEST(ec_app_tests, test_lpc_init_mask)
{
	zassert_unreachable("TODO: Implement this test.");
}
#endif

#ifdef HAS_TASK_KEYSCAN
ZTEST(ec_app_tests, test_keyboard_scan_init)
{
	zassert_unreachable("TODO: Implement this test.");
}
#endif

#if defined(CONFIG_DEDICATED_RECOVERY_BUTTON) || defined(CONFIG_VOLUME_BUTTONS)
ZTEST(ec_app_tests, test_button_init)
{
	zassert_unreachable("TODO: Implement this test.");
}
#endif

#ifdef CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI
ZTEST(ec_app_tests, test_setup_espi)
{
	zassert_unreachable("TODO: Implement this test.");
}
#endif

#ifdef CONFIG_PLATFORM_EC_WATCHDOG
ZTEST(ec_app_tests, test_watchdog_init)
{
	zassert_unreachable("TODO: Implement this test.");
}
#endif

#ifdef CONFIG_PLATFORM_EC_VBOOT_EFS2
ZTEST(ec_app_tests, test_vboot_main)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	/* vboot_main logs the message "VB Verifying hash" */
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(strstr(outbuffer, "VB Verifying hash") != NULL,
		     "'VB Verifying hash' not found in %s", outbuffer);
}
#endif

#ifdef CONFIG_PLATFORM_EC_HOOKS
static int sample_init_hook_count;
/**
 * Just a sample hook.
 */
static void sample_init_hook(void)
{
	printk("Running hook.\n");
	sample_init_hook_count++;
}
DECLARE_HOOK(HOOK_INIT, sample_init_hook, HOOK_PRIO_DEFAULT);

/**
 * @brief Test EC App main runs hooks of type HOOK_INIT.
 *
 * This test installs a hook, runs main and verifies that the hook ran.
 *
 */
ZTEST(ec_app_tests, test_hook_notify_init)
{
	zassert_equal(1, sample_init_hook_count,
		      "Expected sample_init_hook to run once.");
}
#endif

#ifdef CONFIG_SHIMMED_TASKS
ZTEST(ec_app_tests, test_start_ec_tasks)
{
	zassert_equal(task_start_called(), 1, "Tasks did not start.");
}
#endif

/* Does setup for all of the test cases. */
void *ec_app_setup(void)
{
#ifdef CONFIG_SHIMMED_TASKS
	zassert_equal(task_start_called(), 0, "Tasks have already started.");
#endif
	ec_app_main();
	return NULL;
}

ZTEST_SUITE(ec_app_tests, NULL, ec_app_setup, NULL, NULL, NULL);
