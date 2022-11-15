/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_tasks.h"
#include "panic.h"
#include "string.h"
#include "system.h"
#include "system_safe_mode.h"

#include <zephyr/arch/cpu.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>

static bool thread_is_safe_mode_critical(const struct k_thread *thread)
{
	/* List of safe mode critical tasks */
	static const char *safe_mode_critical_threads[] = {
		"main",
		"sysworkq",
		"idle",
		"HOSTCMD",
	};
	for (int i = 0; i < ARRAY_SIZE(safe_mode_critical_threads); i++)
		if (strcmp(thread->name, safe_mode_critical_threads[i]) == 0)
			return true;
	return false;
}

__override bool current_task_is_safe_mode_critical(void)
{
	return thread_is_safe_mode_critical(k_current_get());
}

static void abort_non_critical_threads_cb(const struct k_thread *thread,
					  void *user_data)
{
	ARG_UNUSED(user_data);

	/*
	 * Don't abort if thread is critical or current thread.
	 * Current thread will be canceled automatically after returning from
	 * exception handler.
	 */
	if (thread_is_safe_mode_critical(thread) || k_current_get() == thread)
		return;

	printk("Aborting thread %s\n", thread->name);
	k_thread_abort((struct k_thread *)thread);
}

__override int disable_non_safe_mode_critical_tasks(void)
{
	k_thread_foreach(abort_non_critical_threads_cb, NULL);
	return EC_SUCCESS;
}

static void safe_mode_timeout_cb(struct k_timer *unused)
{
	handle_system_safe_mode_timeout();
}
K_TIMER_DEFINE(safe_mode_timeout, safe_mode_timeout_cb, NULL);

__override int schedule_system_safe_mode_timeout(void)
{
	k_timer_start(&safe_mode_timeout,
		      K_MSEC(CONFIG_PLATFORM_EC_SYSTEM_SAFE_MODE_TIMEOUT_MSEC),
		      K_NO_WAIT);
	return EC_SUCCESS;
}
