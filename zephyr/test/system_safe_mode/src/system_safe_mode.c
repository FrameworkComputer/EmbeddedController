/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_tasks.h"
#include "host_command.h"
#include "panic.h"
#include "system.h"
#include "system_fake.h"
#include "system_safe_mode.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(system_reset, int);

static void system_before(void *data)
{
	RESET_FAKE(system_reset);
	set_system_safe_mode(false);
	get_panic_data_write()->flags = 0;
	system_set_shrspi_image_copy(EC_IMAGE_RW);
}

static void enter_safe_mode_cb(struct k_timer *unused)
{
	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);
}
K_TIMER_DEFINE(enter_safe_mode, enter_safe_mode_cb, NULL);

ZTEST_USER(system_safe_mode, test_safe_mode_from_critical_task)
{
	/**
	 * Timer callback will run in sysworkq, which is a critical
	 * thread, so safe mode should not run.
	 */
	k_timer_start(&enter_safe_mode, K_NO_WAIT, K_NO_WAIT);
	/* Short wait to ensure enter_safe_mode_cb has a chance to run */
	k_msleep(100);
	zassert_false(system_is_in_safe_mode());
	zassert_false(get_panic_data_write()->flags &
		      PANIC_DATA_FLAG_SAFE_MODE_STARTED);
	zassert_true(get_panic_data_write()->flags &
		     PANIC_DATA_FLAG_SAFE_MODE_FAIL_PRECONDITIONS);
	zassert_equal(1, system_reset_fake.call_count);
}

ZTEST_USER(system_safe_mode, test_enter_safe_mode_from_ro)
{
	system_set_shrspi_image_copy(EC_IMAGE_RO);
	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);
	zassert_false(system_is_in_safe_mode());
	zassert_false(get_panic_data_write()->flags &
		      PANIC_DATA_FLAG_SAFE_MODE_STARTED);
	zassert_true(get_panic_data_write()->flags &
		     PANIC_DATA_FLAG_SAFE_MODE_FAIL_PRECONDITIONS);
	zassert_equal(1, system_reset_fake.call_count);
}

ZTEST_USER(system_safe_mode, test_enter_safe_mode_from_kernel_panic)
{
	system_set_shrspi_image_copy(EC_IMAGE_RO);
	k_sys_fatal_error_handler(K_ERR_KERNEL_PANIC, NULL);
	zassert_false(system_is_in_safe_mode());
	zassert_false(get_panic_data_write()->flags &
		      PANIC_DATA_FLAG_SAFE_MODE_STARTED);
	zassert_true(get_panic_data_write()->flags &
		     PANIC_DATA_FLAG_SAFE_MODE_FAIL_PRECONDITIONS);
	zassert_equal(1, system_reset_fake.call_count);
}

ZTEST_USER(system_safe_mode, test_enter_safe_mode_twice)
{
	zassert_false(system_is_in_safe_mode());

	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);
	zassert_true(system_is_in_safe_mode());
	zassert_true(get_panic_data_write()->flags &
		     PANIC_DATA_FLAG_SAFE_MODE_STARTED);
	zassert_false(get_panic_data_write()->flags &
		      PANIC_DATA_FLAG_SAFE_MODE_FAIL_PRECONDITIONS);
	zassert_equal(0, system_reset_fake.call_count);

	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);
	zassert_true(system_is_in_safe_mode());
	zassert_true(get_panic_data_write()->flags &
		     PANIC_DATA_FLAG_SAFE_MODE_STARTED);
	zassert_true(get_panic_data_write()->flags &
		     PANIC_DATA_FLAG_SAFE_MODE_FAIL_PRECONDITIONS);
	zassert_equal(1, system_reset_fake.call_count);
}

ZTEST_USER(system_safe_mode, test_enter_safe_mode)
{
	zassert_false(system_is_in_safe_mode());

	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);
	zassert_equal(0, system_reset_fake.call_count);
	zassert_true(system_is_in_safe_mode());
	zassert_true(get_panic_data_write()->flags &
		     PANIC_DATA_FLAG_SAFE_MODE_STARTED);
	zassert_false(get_panic_data_write()->flags &
		      PANIC_DATA_FLAG_SAFE_MODE_FAIL_PRECONDITIONS);
}

ZTEST_USER(system_safe_mode, test_safe_mode_reboot)
{
	zassert_false(system_is_in_safe_mode());
	k_sys_fatal_error_handler(0, NULL);
	zassert_true(system_is_in_safe_mode());
	zassert_equal(0, system_reset_fake.call_count);

	/* Wait half of timeout and system hasn't rebooted yet */
	k_msleep(CONFIG_PLATFORM_EC_SYSTEM_SAFE_MODE_TIMEOUT_MSEC / 2);
	zassert_equal(0, system_reset_fake.call_count);

	k_msleep(CONFIG_PLATFORM_EC_SYSTEM_SAFE_MODE_TIMEOUT_MSEC / 2);
	zassert_equal(1, system_reset_fake.call_count);
}

ZTEST_USER(system_safe_mode, test_blocked_command_in_safe_mode)
{
	struct ec_params_gpio_get cmd_params = {
		.name = "wp_l",
	};
	struct ec_response_gpio_get cmd_response;

	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_GPIO_GET, 0, cmd_response, cmd_params);

	zassert_false(system_is_in_safe_mode());
	zassert_ok(host_command_process(&args));

	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);

	zassert_true(system_is_in_safe_mode());
	zassert_true(host_command_process(&args));
}

ZTEST_USER(system_safe_mode, test_panic_event_notify)
{
#ifdef CONFIG_HOSTCMD_X86
	/* Enable the EC_HOST_EVENT_PANIC event in the lpc mask */
	host_event_t lpc_event_mask;
	host_event_t mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_PANIC);

	lpc_event_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, lpc_event_mask | mask);
#endif

	zassert_false(host_is_event_set(EC_HOST_EVENT_PANIC));
	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);
	/* Short sleep to allow hook task to run */
	k_msleep(1);
	zassert_true(host_is_event_set(EC_HOST_EVENT_PANIC));
}

ZTEST_SUITE(system_safe_mode, NULL, NULL, system_before, NULL, NULL);
