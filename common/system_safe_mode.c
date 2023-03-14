/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "panic.h"
#include "stddef.h"
#include "system.h"
#include "system_safe_mode.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

static bool in_safe_mode;

static const int safe_mode_allowed_hostcmds[] = {
	EC_CMD_SYSINFO,		EC_CMD_GET_PROTOCOL_INFO,
	EC_CMD_GET_VERSION,	EC_CMD_CONSOLE_SNAPSHOT,
	EC_CMD_CONSOLE_READ,	EC_CMD_GET_NEXT_EVENT,
	EC_CMD_GET_UPTIME_INFO, EC_CMD_GET_PANIC_INFO
};

bool is_task_safe_mode_critical(task_id_t task_id)
{
	const task_id_t safe_mode_critical_tasks[] = {
#ifdef HAS_TASK_HOOKS
		TASK_ID_HOOKS,
#endif
		TASK_ID_IDLE,
#ifdef HAS_TASK_HOSTCMD
		TASK_ID_HOSTCMD,
#endif
#ifdef HAS_TASK_MAIN
		TASK_ID_MAIN,
#endif
#ifdef HAS_TASK_SYSWORKQ
		TASK_ID_SYSWORKQ,
#endif
	};
	for (int i = 0; i < ARRAY_SIZE(safe_mode_critical_tasks); i++)
		if (safe_mode_critical_tasks[i] == task_id)
			return true;
	return false;
}

bool is_current_task_safe_mode_critical(void)
{
	return is_task_safe_mode_critical(task_get_current());
}

#ifndef CONFIG_ZEPHYR

int disable_non_safe_mode_critical_tasks(void)
{
	for (task_id_t task_id = 0; task_id < TASK_ID_COUNT; task_id++) {
		if (!is_task_safe_mode_critical(task_id)) {
			task_disable_task(task_id);
		}
	}
	return EC_SUCCESS;
}

#endif /* CONFIG_ZEPHYR */

void handle_system_safe_mode_timeout(void)
{
	panic_printf("Safe mode timeout after %d msec\n",
		     CONFIG_SYSTEM_SAFE_MODE_TIMEOUT_MSEC);
	panic_reboot();
}
DECLARE_DEFERRED(handle_system_safe_mode_timeout);

__overridable int schedule_system_safe_mode_timeout(void)
{
	hook_call_deferred(&handle_system_safe_mode_timeout_data,
			   CONFIG_SYSTEM_SAFE_MODE_TIMEOUT_MSEC * MSEC);
	return EC_SUCCESS;
}

bool system_is_in_safe_mode(void)
{
	return !!in_safe_mode;
}

bool command_is_allowed_in_safe_mode(int command)
{
	for (int i = 0; i < ARRAY_SIZE(safe_mode_allowed_hostcmds); i++)
		if (command == safe_mode_allowed_hostcmds[i])
			return true;
	return false;
}

static void system_safe_mode_start(void)
{
	if (IS_ENABLED(CONFIG_HOSTCMD_EVENTS))
		host_set_single_event(EC_HOST_EVENT_PANIC);
}
DECLARE_DEFERRED(system_safe_mode_start);

int start_system_safe_mode(void)
{
	if (!system_is_in_rw()) {
		panic_printf("Can only enter safe mode from RW image\n");
		return EC_ERROR_INVAL;
	}

	if (system_is_in_safe_mode()) {
		panic_printf("Already in system safe mode");
		return EC_ERROR_INVAL;
	}

	if (is_current_task_safe_mode_critical()) {
		/* TODO: Restart critical tasks */
		panic_printf(
			"Fault in critical task, cannot enter system safe mode\n");
		return EC_ERROR_INVAL;
	}

	disable_non_safe_mode_critical_tasks();

	schedule_system_safe_mode_timeout();

	/*
	 * Schedule a deferred function to run immediately
	 * after returning from fault handler. Defer operations that
	 * must not run in an ISR to this function.
	 */
	hook_call_deferred(&system_safe_mode_start_data, 0);

	in_safe_mode = true;

	panic_printf("\nStarting system safe mode\n");

	return EC_SUCCESS;
}

#ifdef TEST_BUILD
void set_system_safe_mode(bool mode)
{
	in_safe_mode = mode;
}
#endif
