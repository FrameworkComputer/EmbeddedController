/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "host_command.h"
#include "panic.h"
#include "system.h"
#include "system_fake.h"
#include "system_safe_mode.h"
#include "uart.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <regex.h>

FAKE_VOID_FUNC(system_reset, int);

static void system_before(void *data)
{
	RESET_FAKE(system_reset);
	reset_system_safe_mode();
	get_panic_data_write()->flags = 0;
	system_set_shrspi_image_copy(EC_IMAGE_RW);
	shell_start(get_ec_shell());
}

static void enter_safe_mode_cb(struct k_timer *unused)
{
	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);
}
K_TIMER_DEFINE(enter_safe_mode, enter_safe_mode_cb, NULL);

ZTEST_USER(system_safe_mode, test_feature_present)
{
	struct ec_response_get_features feat;

	zassert_ok(ec_cmd_get_features(NULL, &feat), "Failed to get features");

	zassert_true(feat.flags[1] &
		     EC_FEATURE_MASK_1(EC_FEATURE_SYSTEM_SAFE_MODE));
}

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

	zassert_false(system_is_in_safe_mode());
	zassert_ok(ec_cmd_gpio_get(NULL, &cmd_params, &cmd_response));

	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);

	zassert_true(system_is_in_safe_mode());
	zassert_true(ec_cmd_gpio_get(NULL, &cmd_params, &cmd_response));
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

static uint32_t fake_stack[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
	0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
	0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

uint32_t get_panic_stack_pointer(const struct panic_data *pdata)
{
	return (uint32_t)fake_stack;
}

ZTEST_USER(system_safe_mode, test_print_stack_contents)
{
	char buffer[1024];
	uint16_t write_count;
	regex_t regex;

	char *regex_str = "Stack Contents\n"
			  "[0-9a-f]{8}: 00000000 00000001 00000002 00000003\n"
			  "[0-9a-f]{8}: 00000004 00000005 00000006 00000007\n"
			  "[0-9a-f]{8}: 00000008 00000009 0000000a 0000000b\n"
			  "[0-9a-f]{8}: 0000000c 0000000d 0000000e 0000000f\n"
			  "[0-9a-f]{8}: 00000010 00000011 00000012 00000013\n"
			  "[0-9a-f]{8}: 00000014 00000015 00000016 00000017\n"
			  "[0-9a-f]{8}: 00000018 00000019 0000001a 0000001b\n"
			  "[0-9a-f]{8}: 0000001c 0000001d 0000001e 0000001f\n";

	zassert_ok(regcomp(&regex, regex_str, REG_EXTENDED));

	/* Snapshot console before panic */
	zassert_ok(uart_console_read_buffer_init(), NULL);

	k_sys_fatal_error_handler(K_ERR_CPU_EXCEPTION, NULL);
	/* Short sleep to allow hook task to run */
	k_msleep(1);
	zassert_true(system_is_in_safe_mode());

	/* Snapshot console after panic */
	zassert_ok(uart_console_read_buffer_init(), NULL);

	zassert_ok(uart_console_read_buffer(CONSOLE_READ_RECENT, buffer,
					    sizeof(buffer), &write_count),
		   NULL);
	zassert_true(write_count > 0);

	/* Check for expected stack print in console buffer */
	zassert_ok(regexec(&regex, buffer, 0, NULL, 0));
	regfree(&regex);
}

ZTEST_SUITE(system_safe_mode, NULL, NULL, system_before, NULL, NULL);
