/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_tasks.h"
#include "host_command.h"
#include "task.h"
#include "zephyr_console_shim.h"

#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>
#include <zephyr/ztest.h>

k_tid_t get_sysworkq_thread(void);
k_tid_t get_idle_thread(void);

/* Utilities for finding a Zephyr thread by name */
static k_tid_t found_thread;
static void find_thread_by_name_cb(const struct k_thread *thread,
				   void *user_data)
{
	const char *name = (const char *)user_data;

	if (strcmp(k_thread_name_get((k_tid_t)thread), name) == 0) {
		found_thread = (k_tid_t)thread;
	}
}

static k_tid_t find_thread_by_name(const char *name)
{
	found_thread = NULL;
	k_thread_foreach_unlocked(find_thread_by_name_cb, (void *)name);
	return found_thread;
}

/* Utilities for checking asserts */
static bool expect_assert;
static int num_asserts;
void assert_post_action(const char *file, unsigned int line)
{
	num_asserts += 1;
	if (!expect_assert) {
		ztest_test_fail();
	}
}

#define EXPECT_ASSERT(test)                    \
	do {                                   \
		expect_assert = true;          \
		num_asserts = 0;               \
		(test);                        \
		expect_assert = false;         \
		zassert_equal(num_asserts, 1); \
	} while (0)

ZTEST_USER(extra_tasks, test_hostcmd_thread_mapping)
{
	k_tid_t hostcmd_thread;
	k_tid_t main_thread;

#ifdef HAS_TASK_HOSTCMD
#ifdef CONFIG_TASK_HOSTCMD_THREAD_MAIN
	k_thread_name_set(get_main_thread(), "HOSTCMD");
#endif /* CONFIG_TASK_HOSTCMD_THREAD_MAIN */

	hostcmd_thread = find_thread_by_name("HOSTCMD");
	zassert_not_null(hostcmd_thread);
	zassert_equal(hostcmd_thread, get_hostcmd_thread());
	zassert_equal(TASK_ID_HOSTCMD, thread_id_to_task_id(hostcmd_thread));
	zassert_equal(task_id_to_thread_id(TASK_ID_HOSTCMD), hostcmd_thread);

#ifdef CONFIG_TASK_HOSTCMD_THREAD_DEDICATED
	main_thread = find_thread_by_name("main");
	zassert_not_null(main_thread);
	zassert_equal(main_thread, get_main_thread());
	zassert_not_equal(main_thread, hostcmd_thread);
	zassert_equal(TASK_ID_MAIN, thread_id_to_task_id(main_thread));
	zassert_equal(task_id_to_thread_id(TASK_ID_MAIN), main_thread);
#else
	main_thread = get_main_thread();
	zassert_not_null(main_thread);
	zassert_equal(main_thread, hostcmd_thread);
#endif /* CONFIG_TASK_HOSTCMD_THREAD_DEDICATED */

#else /* !HAS_TASK_HOSTCMD */
	hostcmd_thread = find_thread_by_name("HOSTCMD");
	zassert_is_null(hostcmd_thread);
	EXPECT_ASSERT(hostcmd_thread = get_hostcmd_thread());
	zassert_is_null(hostcmd_thread);

	main_thread = find_thread_by_name("main");
	zassert_not_null(main_thread);
	zassert_equal(main_thread, get_main_thread());
#endif /* HAS_TASK_HOSTCMD */
}

ZTEST_USER(extra_tasks, test_sysworkq_thread_mapping)
{
	k_tid_t sysworkq_thread;

	sysworkq_thread = find_thread_by_name("sysworkq");
	zassert_not_null(sysworkq_thread);
	zassert_equal(sysworkq_thread, get_sysworkq_thread());
	zassert_equal(TASK_ID_SYSWORKQ, thread_id_to_task_id(sysworkq_thread));
	zassert_equal(task_id_to_thread_id(TASK_ID_SYSWORKQ), sysworkq_thread);
}

ZTEST_USER(extra_tasks, test_idle_thread_mapping)
{
	k_tid_t idle_thread;

	idle_thread = find_thread_by_name("idle");
	zassert_not_null(idle_thread);
	zassert_equal(idle_thread, get_idle_thread());
	zassert_equal(TASK_ID_IDLE, thread_id_to_task_id(idle_thread));
	zassert_equal(task_id_to_thread_id(TASK_ID_IDLE), idle_thread);
}

ZTEST_USER(extra_tasks, test_shell_thread_to_task_mapping)
{
	k_tid_t shell_thread;

	shell_thread = find_thread_by_name("shell_uart");
	zassert_not_null(shell_thread);
	zassert_equal(shell_thread, get_shell_thread());
	zassert_equal(TASK_ID_SHELL, thread_id_to_task_id(shell_thread));
	zassert_equal(task_id_to_thread_id(TASK_ID_SHELL), shell_thread);
}

ZTEST_USER(extra_tasks, test_invalid_task_id)
{
	k_tid_t thread_id;

	EXPECT_ASSERT(thread_id = task_id_to_thread_id(TASK_ID_INVALID));
	zassert_is_null(thread_id);

	EXPECT_ASSERT(thread_id = task_id_to_thread_id(-1));
	zassert_is_null(thread_id);
}

ZTEST_USER(extra_tasks, test_invalid_thread_id)
{
	task_id_t task_id;

	EXPECT_ASSERT(task_id = thread_id_to_task_id(NULL));
	zassert_equal(task_id, TASK_ID_INVALID);

	EXPECT_ASSERT(task_id = thread_id_to_task_id((k_tid_t)0x1234));
	zassert_equal(task_id, TASK_ID_INVALID);
}

ZTEST_USER(extra_tasks, test_extra_task_enumeration)
{
	for (task_id_t task_id = 0; task_id < TASK_ID_COUNT + EXTRA_TASK_COUNT;
	     task_id++) {
		zassert_not_null(task_id_to_thread_id(task_id));
	}
}

ZTEST_SUITE(extra_tasks, NULL, NULL, NULL, NULL, NULL);
