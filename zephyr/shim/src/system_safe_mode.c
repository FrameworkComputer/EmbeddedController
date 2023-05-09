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

__override int disable_non_safe_mode_critical_tasks(void)
{
	for (task_id_t task_id = 0; task_id < TASK_ID_COUNT + EXTRA_TASK_COUNT;
	     task_id++) {
		k_tid_t thread_id = task_id_to_thread_id(task_id);

		/* Don't abort the thread when:
		 * 1) The task to thread lookup failed
		 * 2) It's the current thread since it will be aborted
		 *    automatically by the Zephyr kernel.
		 * 3) The thread is safe mode critical
		 */
		if (thread_id == NULL || thread_id == k_current_get() ||
		    is_task_safe_mode_critical(task_id)) {
			continue;
		}
		k_thread_abort(thread_id);
	}
	return EC_SUCCESS;
}

static void safe_mode_timeout_cb(struct k_timer *unused)
{
	handle_system_safe_mode_timeout();
}
K_TIMER_DEFINE(safe_mode_timeout, safe_mode_timeout_cb, NULL);
