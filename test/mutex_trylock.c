/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

K_MUTEX_DEFINE(mtx);

int mutex_second_task(void *unused)
{
	task_id_t id = task_get_current();

	ccprintf("\n[Mutex second task %d]\n", id);

	task_wait_event(0);
	ccprintf("MTX2: Attempt to lock (should return 1)...\n");
	TEST_EQ(mutex_try_lock(&mtx), 1, "%d");
	ccprintf("done\n");
	task_wake(TASK_ID_MTX1);
	ccprintf("MTX2: Unlocking...\n");
	mutex_unlock(&mtx);

	task_wait_event(0);

	return EC_SUCCESS;
}

int mutex_main_task(void *unused)
{
	task_id_t id = task_get_current();

	ccprintf("\n[Mutex main task %d]\n", id);

	task_wait_event(0);

	/* --- Try lock/Unlock without contention --- */
	ccprintf("No contention :\n");
	TEST_EQ(mutex_try_lock(&mtx), 1, "%d");
	TEST_EQ(mutex_try_lock(&mtx), 0, "%d");
	mutex_unlock(&mtx);
	TEST_EQ(mutex_try_lock(&mtx), 1, "%d");
	TEST_EQ(mutex_try_lock(&mtx), 0, "%d");
	mutex_unlock(&mtx);
	TEST_EQ(mutex_try_lock(&mtx), 1, "%d");
	TEST_EQ(mutex_try_lock(&mtx), 0, "%d");
	mutex_unlock(&mtx);
	ccprintf("done.\n");

	/* --- Serialization to test simple contention --- */
	ccprintf("Simple contention :\n");
	/* Attempt to lock the mutex from the other task */
	task_set_event(TASK_ID_MTX2, TASK_EVENT_WAKE);
	task_wait_event(0);
	/* Check if mutex is locked (mutex_try_lock() returns 0) */
	ccprintf("MTX1: Attempt to lock (should return 0)...\n");
	TEST_EQ(mutex_try_lock(&mtx), 0, "%d");
	/* Block on the mutex */
	ccprintf("MTX1: Blocking...\n");
	mutex_lock(&mtx);
	ccprintf("MTX1: Got lock\n");
	mutex_unlock(&mtx);

	test_pass();
	task_wait_event(0);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	wait_for_task_started();
	/* Start mutex_main_task() */
	task_wake(TASK_ID_MTX1);
}
