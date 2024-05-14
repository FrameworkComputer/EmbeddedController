/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#ifndef __CROS_EC_EC_TASKS_H
#define __CROS_EC_EC_TASKS_H

#include "task.h"

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Starts all of the shimmed EC tasks. Requires CONFIG_SHIMMED_TASKS=y. */
void start_ec_tasks(void);

/**
 * Maps an EC task id to a Zephyr thread id.
 *
 * @returns Thread id OR NULL if mapping fails
 */
k_tid_t task_id_to_thread_id(task_id_t task_id);

/**
 * Maps a Zephyr thread id to an EC task id.
 *
 * @returns Task id OR TASK_ID_INVALID if mapping fails
 */
task_id_t thread_id_to_task_id(k_tid_t thread_id);

#ifdef TEST_BUILD
/**
 * Set TASK_ID_TEST_RUNNER to current thread tid. Some functions that are tested
 * require to run in any task context.
 */
void set_test_runner_tid(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_EC_TASKS_H */
