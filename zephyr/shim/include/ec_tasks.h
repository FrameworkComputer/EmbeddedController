/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_EC_TASKS_H
#define __CROS_EC_EC_TASKS_H

/*
 * The lowest preemptive thread priority is (CONFIG_NUM_PREEMT_PRIORITIES-1)
 * while the lowest cooperative thread priority is -1.
 *
 * https://docs.zephyrproject.org/latest/reference/kernel/threads/index.html#thread-priorities
 */
#define LOWEST_THREAD_PRIORITY              \
	COND_CODE_1(CONFIG_PREEMPT_ENABLED, \
		    (CONFIG_NUM_PREEMPT_PRIORITIES - 1), (-1))

/** Starts all of the shimmed EC tasks. Requires CONFIG_SHIMMED_TASKS=y. */
void start_ec_tasks(void);

#ifdef TEST_BUILD
/**
 * Set TASK_ID_TEST_RUNNER to current thread tid. Some functions that are tested
 * require to run in any task context.
 */
void set_test_runner_tid(void);
#endif

#endif /* __CROS_EC_EC_TASKS_H */
