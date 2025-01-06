/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(libc_exit, LOG_LEVEL_INF);

#define STACK_SIZE (512 + CONFIG_TEST_EXTRA_STACK_SIZE)
static struct k_thread tdata;
static K_THREAD_STACK_DEFINE(tstack, STACK_SIZE);

static ZTEST_BMEM int execute_flag;

ZTEST_SUITE(libc_exit, NULL, NULL, NULL, NULL, NULL);

static void thread_call_exit(void *p1, void *p2, void *p3)
{
	execute_flag = 1;

	ztest_set_fault_valid(true);
	exit(1);

	/* Should never reach this. */
	ztest_test_fail();
}

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	zassert_equal(reason, K_ERR_KERNEL_PANIC);
	zassert_true(execute_flag == 1);
}

ZTEST_USER(libc_exit, test_non_essential_thread)
{
	execute_flag = 0;

	/* Testing a non-essential thread. */
	k_tid_t tid;
	tid = k_thread_create(&tdata, tstack, STACK_SIZE, thread_call_exit,
			      NULL, NULL, NULL, 0, K_USER, K_NO_WAIT);
	k_thread_join(tid, K_FOREVER);

	/*
	 * TESTPOINT: Spawned thread executed but aborted itself. The
	 * 'execute_flag' is set to one, demonstrating that the thread ran its
	 * task and performed a self-abort without triggering a system reboot.
	 * This confirms the proper execution and controlled termination of the
	 * thread.
	 */
	zassert_true(execute_flag == 1);
}

ZTEST_USER(libc_exit, test_essential_thread)
{
	/* Testing an essential thread. */
	k_tid_t tid;
	tid = k_thread_create(&tdata, tstack, STACK_SIZE, thread_call_exit,
			      NULL, NULL, NULL, 0, K_ESSENTIAL, K_NO_WAIT);
	k_thread_join(tid, K_FOREVER);
}
