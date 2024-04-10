/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "libc/recursive_mutex.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

K_MUTEX_R_DEFINE(mtx);

/* Period between 50us and 3.2ms */
#define PERIOD_US(num) (((num % 64) + 1) * 50)
/* One of the 3 MTX3x tasks */
#define RANDOM_TASK(num) (TASK_ID_MTX3C + (num % 3))

int mutex_recursive_random_task(void *unused)
{
	char letter = 'A' + (TASK_ID_MTX3A - task_get_current());

	while (1) {
		task_wait_event(0);
		for (int i = 1; i <= 5; i++) {
			ccprintf("%c+\n", letter);
			mutex_lock_recursive(&mtx);
			crec_usleep(50);
		}

		ccprintf("%c=\n", letter);
		task_wait_event(0);

		for (int i = 1; i <= 5; i++) {
			ccprintf("%c-\n", letter);
			mutex_unlock_recursive(&mtx);
		}
	}

	task_wait_event(0);

	return EC_SUCCESS;
}

int mutex_recursive_second_task(void *unused)
{
	task_id_t id = task_get_current();

	ccprintf("\n[Recursive mutex second task %d]\n", id);

	task_wait_event(0);
	for (int i = 1; i <= 5; i++) {
		ccprintf("MTX2: Locking (%d)...", i);
		mutex_lock_recursive(&mtx);
		ccprintf("done\n");
	}

	/* Ping main task */
	task_wake(TASK_ID_MTX1);

	for (int i = 5; i > 0; i--) {
		ccprintf("MTX2: Unlocking (%d)...\n", i);
		mutex_unlock_recursive(&mtx);

		/*
		 * Ping MTX1 task to confirm that it's not able to take
		 * a mutex locked by this thread.
		 */
		if (i > 1) {
			ccprintf("MTX2: Ping MTX1 task\n");
			task_wake(TASK_ID_MTX1);
		}
	}

	task_wait_event(0);

	return EC_SUCCESS;
}

int mutex_recursive_main_task(void *unused)
{
	task_id_t id = task_get_current();
	uint32_t rdelay = (uint32_t)0x0bad1dea;
	uint32_t rtask = (uint32_t)0x1a4e1dea;
	int i;

	ccprintf("\n[Recursive mutex main task %d]\n", id);

	task_wait_event(0);

	/* Lock/Unlock without contention. */
	ccprintf("No contention :\n");
	for (i = 0; i < 10; i++) {
		mutex_lock_recursive(&mtx);
		TEST_EQ(mutex_try_lock_recursive(&mtx), 1, "%d");
	}
	for (i = 0; i < 20; i++) {
		mutex_unlock_recursive(&mtx);
	}
	ccprintf("done.\n");

	/* Serialization to test simple contention. */
	ccprintf("Simple contention :\n");

	/* Lock the mutex from the other task */
	task_set_event(TASK_ID_MTX2, TASK_EVENT_WAKE);
	task_wait_event(0);

	/* Check if mutex_try_lock_recursive() returns 0 */
	ccprintf(
		"MTX1: Confirm that the try_lock won't give us the mutex...\n");
	TEST_EQ(mutex_try_lock_recursive(&mtx), 0, "%d");

	/* Block on the mutex */
	ccprintf("MTX1: Blocking...\n");
	mutex_lock_recursive(&mtx);
	ccprintf("MTX1: Got lock\n");
	mutex_unlock_recursive(&mtx);

	/* Mass recursive lock/unlocking from several tasks. */
	ccprintf("Massive locking/unlocking :\n");
	for (i = 0; i < 500; i++) {
		/*
		 * Wake up a random task. Note this doesn't reschedule task
		 * now (will reschedule when task_wait_event() is called)
		 */
		task_wake(RANDOM_TASK(rtask));

		/* Next pseudo random task */
		rtask = prng(rtask);

		/* Wait for a "random" period */
		task_wait_event(PERIOD_US(rdelay));

		/* Next pseudo random delay */
		rdelay = prng(rdelay);
	}

	test_pass();
	task_wait_event(0);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	wait_for_task_started();

	/* Start mutex_recursive_main_task() */
	task_wake(TASK_ID_MTX1);
}
