/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "host_command_memory_dump.h"
#include "string.h"

#include <zephyr/arch/cpu.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>

static bool task_is_excluded_from_memory_dump(task_id_t task_id)
{
	/* List of sensitive threads that must be excluded from memory dump */
	static const task_id_t memory_dump_exclude_tasks[] = {
		COND_CODE_1(CONFIG_HAS_TASK_KEYSCAN, (TASK_ID_KEYSCAN), ()),
		COND_CODE_1(CONFIG_HAS_TASK_KEYPROTO, (TASK_ID_KEYPROTO), ()),
		COND_CODE_1(CONFIG_HAS_TASK_WOV, (TASK_ID_WOV), ())
	};
	for (int i = 0; i < ARRAY_SIZE(memory_dump_exclude_tasks); i++) {
		if (task_id == memory_dump_exclude_tasks[i]) {
			return true;
		}
	}
	return false;
}

test_export_static void register_thread_memory_dump(k_tid_t thread)
{
	register_memory_dump(thread->stack_info.start,
			     thread->stack_info.size -
				     thread->stack_info.delta);
}

test_export_static void register_known_threads_memory_dump(void)
{
	k_tid_t thread;

	for (task_id_t task_id = 0; task_id < TASK_ID_COUNT + EXTRA_TASK_COUNT;
	     task_id++) {
		if (task_is_excluded_from_memory_dump(task_id)) {
			continue;
		}
		thread = task_id_to_thread_id(task_id);
		if (thread == NULL) {
			continue;
		}
		register_thread_memory_dump(thread);
	}
}
DECLARE_HOOK(HOOK_INIT, register_known_threads_memory_dump, HOOK_PRIO_FIRST);
