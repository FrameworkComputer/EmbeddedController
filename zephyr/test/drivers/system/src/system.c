/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "mock/power.h"
#include "panic.h"
#include "system.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, system_run_image_copy_with_flags, enum ec_image, int);
FAKE_VOID_FUNC(system_disable_jump);
FAKE_VOID_FUNC(jump_to_image, uintptr_t);

/* System Host Commands */

ZTEST_USER(system, test_hostcmd_sysinfo)
{
	struct ec_response_sysinfo response;
	struct host_cmd_handler_args args;

	/* Simply issue the command and get the results */
	zassert_ok(ec_cmd_sysinfo(&args, &response), NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.reset_flags, 0, "response.reset_flags = %d",
		      response.reset_flags);
	zassert_equal(response.current_image, EC_IMAGE_RO,
		      "response.current_image = %d", response.current_image);
	zassert_equal(response.flags, 0, "response.flags = %d", response.flags);
}

/* System Function Testing */

static void system_before_after(void *data)
{
	ARG_UNUSED(data);
	system_clear_reset_flags(-1);

	RESET_FAKE(system_run_image_copy_with_flags);
	RESET_FAKE(system_disable_jump);
	RESET_FAKE(jump_to_image);
}

ZTEST(system, test_system_enter_hibernate__at_g3)
{
	set_ac_enabled(false);
	test_set_chipset_to_g3();

	/* Reset after set to g3 */
	chipset_force_shutdown_fake.call_count = 0;

	/* Arbitrary Args */
	system_enter_hibernate(0x12, 0x34);
	zassert_equal(chipset_force_shutdown_fake.call_count, 0);
	zassert_equal(system_hibernate_fake.call_count, 1);
}

ZTEST(system, test_system_enter_hibernate__ac_on)
{
	test_set_chipset_to_s0();
	set_ac_enabled(true);

	/* Arbitrary Args */
	system_enter_hibernate(0x12, 0x34);
	zassert_equal(chipset_force_shutdown_fake.call_count, 0);
}

ZTEST(system, test_system_enter_hibernate__at_s0)
{
	test_set_chipset_to_s0();
	set_ac_enabled(false);

	/* Arbitrary Args */
	system_enter_hibernate(0x12, 0x34);

	zassert_equal(chipset_force_shutdown_fake.call_count, 1);
	zassert_equal(chipset_force_shutdown_fake.arg0_val,
		      CHIPSET_SHUTDOWN_CONSOLE_CMD);
}

ZTEST(system, test_get_program_memory_addr_bad_args)
{
	zassert_equal(get_program_memory_addr(-1), INVALID_ADDR);
}

ZTEST(system, test_system_common_pre_init__watch_dog_panic)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	/* Watchdog reset should result in any existing panic data being
	 * overwritten
	 */
	panic_set_reason(PANIC_SW_DIV_ZERO, 0x12, 0x34);

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	system_common_pre_init();
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(reason, PANIC_SW_WATCHDOG);
	zassert_equal(info, 0);
	zassert_equal(exception, 0);
}

ZTEST(system, test_system_common_pre_init__watch_dog_warn_panic)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	/* Panic reason PANIC_SW_WATCHDOG_WARN should be switched
	 * to PANIC_SW_WATCHDOG after a watchdog reset.
	 * Info and exception should be preserved.
	 */
	panic_set_reason(PANIC_SW_WATCHDOG_WARN, 0x12, 0x34);

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	system_common_pre_init();
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(reason, PANIC_SW_WATCHDOG);
	zassert_equal(info, 0x12);
	zassert_equal(exception, 0x34);
}

ZTEST(system, test_system_common_pre_init__watch_dog_panic_already_initialized)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	/* Watchdog reset should not overwrite panic info if already filled
	 * in with watchdog panic info that HAS NOT been read by host
	 */
	panic_set_reason(PANIC_SW_WATCHDOG, 0x12, 0x34);

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	system_common_pre_init();
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(reason, PANIC_SW_WATCHDOG);
	zassert_equal(info, 0x12);
	zassert_equal(exception, 0x34);
}

ZTEST(system, test_system_common_pre_init__watch_dog_panic_already_read)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;
	struct panic_data *pdata;

	/* Watchdog reset should overwrite panic info if already filled
	 * in with watchdog panic info that HAS been read by host
	 */
	panic_set_reason(PANIC_SW_WATCHDOG, 0x12, 0x34);
	pdata = get_panic_data_write();
	pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(EC_RESET_FLAG_WATCHDOG);
	system_common_pre_init();
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(reason, PANIC_SW_WATCHDOG);
	zassert_equal(info, 0);
	zassert_equal(exception, 0);
}

ZTEST(system, test_system_encode_save_flags)
{
	int flags_to_save = 0;
	uint32_t saved_flags = 0;
	int arbitrary_reset_flags = 1;

	/* Save all possible flags */
	flags_to_save = -1;

	/* Clear all reset flags and set them arbitrarily */
	system_set_reset_flags(arbitrary_reset_flags);

	system_encode_save_flags(flags_to_save, &saved_flags);

	/* Verify all non-mutually exclusive flags */
	zassert_equal(1, saved_flags & system_get_reset_flags(), NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_AP_OFF, NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_STAY_IN_RO, NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_AP_WATCHDOG, NULL);
}

ZTEST(system, test_system_encode_save_flags_mutually_exclusive_reset_flags)
{
	int flags_to_save = 0;
	uint32_t saved_flags = 0;

	/* Verify reset hard takes precedence over hibernate/soft */
	flags_to_save = SYSTEM_RESET_HARD | SYSTEM_RESET_HIBERNATE;

	system_encode_save_flags(flags_to_save, &saved_flags);

	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_HARD, NULL);
	zassert_equal(0, saved_flags & EC_RESET_FLAG_HIBERNATE, NULL);
	zassert_equal(0, saved_flags & EC_RESET_FLAG_SOFT, NULL);

	/* Verify reset hibernate takes precedence over soft */
	flags_to_save = SYSTEM_RESET_HIBERNATE;

	system_encode_save_flags(flags_to_save, &saved_flags);

	zassert_equal(0, saved_flags & EC_RESET_FLAG_HARD, NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_HIBERNATE, NULL);
	zassert_equal(0, saved_flags & EC_RESET_FLAG_SOFT, NULL);

	/* Verify reset soft is always saved given no other flags */
	flags_to_save = 0;

	system_encode_save_flags(flags_to_save, &saved_flags);

	zassert_equal(0, saved_flags & EC_RESET_FLAG_HARD, NULL);
	zassert_equal(0, saved_flags & EC_RESET_FLAG_HIBERNATE, NULL);
	zassert_not_equal(0, saved_flags & EC_RESET_FLAG_SOFT, NULL);
}

/* System Console Commands */

ZTEST_USER(system, test_console_cmd_sysjump__no_args)
{
	const struct shell *shell_zephyr = get_ec_shell();

	/* No output from no-arg commands, so just test failure */
	zassert_equal(shell_execute_cmd(shell_zephyr, "sysjump"),
		      EC_ERROR_PARAM_COUNT);
}

ZTEST_USER(system, test_console_cmd_sysjump__RO)
{
	const struct shell *shell_zephyr = get_ec_shell();

	/* Since we start at RO this acts as NOOP */
	zassert_ok(shell_execute_cmd(shell_zephyr, "sysjump RO"));

	zassert_equal(system_run_image_copy_with_flags_fake.call_count, 1);
	zassert_equal(system_run_image_copy_with_flags_fake.arg0_val,
		      EC_IMAGE_RO);
	zassert_equal(system_run_image_copy_with_flags_fake.arg1_val,
		      EC_RESET_FLAG_STAY_IN_RO);
}

ZTEST_USER(system, test_console_cmd_sysjump__RW)
{
	const struct shell *shell_zephyr = get_ec_shell();

	zassert_ok(shell_execute_cmd(shell_zephyr, "sysjump RW"));
	zassert_equal(system_run_image_copy_with_flags_fake.call_count, 1);
	zassert_equal(system_run_image_copy_with_flags_fake.arg0_val,
		      EC_IMAGE_RW);
	zassert_equal(system_run_image_copy_with_flags_fake.arg1_val, 0);
}

ZTEST_USER(system, test_console_cmd_sysjump__A)
{
	const struct shell *shell_zephyr = get_ec_shell();

	zassert_ok(shell_execute_cmd(shell_zephyr, "sysjump A"));
	zassert_equal(system_run_image_copy_with_flags_fake.call_count, 1);
	zassert_equal(system_run_image_copy_with_flags_fake.arg0_val,
		      EC_IMAGE_RW);
	zassert_equal(system_run_image_copy_with_flags_fake.arg1_val, 0);
}

ZTEST_USER(system, test_console_cmd_sysjump__B)
{
	const struct shell *shell_zephyr = get_ec_shell();

	/* Downstream Zephyr isn't setup with A/B images */
	zassert_equal(shell_execute_cmd(shell_zephyr, "sysjump B"),
		      EC_ERROR_PARAM1);
	zassert_equal(system_run_image_copy_with_flags_fake.call_count, 0);
}

ZTEST_USER(system, test_console_cmd_sysjump__disable)
{
	const struct shell *shell_zephyr = get_ec_shell();

	zassert_ok(shell_execute_cmd(shell_zephyr, "sysjump disable"));
	zassert_equal(system_run_image_copy_with_flags_fake.call_count, 0);
	zassert_equal(system_disable_jump_fake.call_count, 1);
}

ZTEST_USER(system, test_console_cmd_sysjump__addr_while_sys_locked)
{
	const struct shell *shell_zephyr = get_ec_shell();

	system_is_locked_fake.return_val = true;

	/* No output to test against */
	zassert_equal(shell_execute_cmd(shell_zephyr, "sysjump 0x1234"),
		      EC_ERROR_ACCESS_DENIED);
	zassert_equal(system_is_locked_fake.call_count, 1);
}

ZTEST_USER(system, test_console_cmd_sysjump__addr)
{
	const struct shell *shell_zephyr = get_ec_shell();

	/* No output to test against */
	zassert_ok(shell_execute_cmd(shell_zephyr, "sysjump 0x1234"));
	zassert_equal(system_is_locked_fake.call_count, 1);
	zassert_equal(jump_to_image_fake.call_count, 1);
	zassert_equal(jump_to_image_fake.arg0_val, 0x1234);
}

ZTEST_USER(system, test_console_cmd_sysjump__addr_bad_number)
{
	const struct shell *shell_zephyr = get_ec_shell();

	/* No output to test against */
	zassert_equal(shell_execute_cmd(shell_zephyr, "sysjump O___o"),
		      EC_ERROR_PARAM1);
	zassert_equal(system_is_locked_fake.call_count, 1);
	zassert_equal(jump_to_image_fake.call_count, 0);
}

ZTEST_SUITE(system, drivers_predicate_post_main, NULL, system_before_after,
	    system_before_after, NULL);
