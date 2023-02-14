/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>
#include <zephyr/ztest.h>

/* Utility functions for finding a Zephyr thread by name */
static k_tid_t found_thread;
static void find_thread_by_name_cb(const struct k_thread *thread,
				   void *user_data)
{
	const char *name = (const char *)user_data;

	if (strcmp(k_thread_name_get((k_tid_t)thread), name) == 0)
		found_thread = (k_tid_t)thread;
}

static k_tid_t find_thread_by_name(const char *name)
{
	found_thread = NULL;
	k_thread_foreach_unlocked(find_thread_by_name_cb, (void *)name);
	return found_thread;
}

ZTEST_USER(extra_tasks, test_main_thread_mapping)
{
	k_tid_t hostcmd_thread;
	k_tid_t main_thread;

	hostcmd_thread = find_thread_by_name("HOSTCMD");
	zassert_not_null(hostcmd_thread);

	main_thread = find_thread_by_name("main");
	zassert_not_null(main_thread);
	/* Not equal when CONFIG_TASK_HOSTCMD_THREAD_DEDICATED is set */
	zassert_not_equal(main_thread, hostcmd_thread);
}

ZTEST_USER(extra_tasks, test_sysworkq_thread_mapping)
{
	k_tid_t sysworkq_thread;

	sysworkq_thread = find_thread_by_name("sysworkq");
	zassert_not_null(sysworkq_thread);
}

ZTEST_USER(extra_tasks, test_idle_thread_mapping)
{
	k_tid_t idle_thread;

	idle_thread = find_thread_by_name("idle");
	zassert_not_null(idle_thread);
}

ZTEST_SUITE(extra_tasks, NULL, NULL, NULL, NULL, NULL);
