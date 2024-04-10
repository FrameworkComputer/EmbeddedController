/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tasks for scheduling test.
 */

#include "common.h"
#include "console.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define TEST_COUNT 3000

static int wake_count[3];

int task_abc(void *data)
{
	int myid = task_get_current() - TASK_ID_TESTA;
	task_id_t next = task_get_current() + 1;
	if (next > TASK_ID_TESTC)
		next = TASK_ID_TESTA;

	task_wait_event(-1);

	ccprintf("\n[starting Task %c]\n", ('A' + myid));

	while (1) {
		wake_count[myid]++;
		if (myid == 2 && wake_count[myid] == TEST_COUNT) {
			if (wake_count[0] == TEST_COUNT &&
			    wake_count[1] == TEST_COUNT)
				test_pass();
			else
				test_fail();
			wake_count[0] = wake_count[1] = wake_count[2] = 0;
			task_wait_event(-1);
		} else {
			task_set_event(next, TASK_EVENT_WAKE);
			task_wait_event(-1);
		}
	}

	return EC_SUCCESS;
}

int task_tick(void *data)
{
	task_wait_event(-1);
	ccprintf("\n[starting Task T]\n");

	/* Wake up every tick */
	while (1) {
		/* Wait for timer interrupt message */
		crec_usleep(3000);
	}

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	wait_for_task_started();
	task_wake(TASK_ID_TICK);
	task_wake(TASK_ID_TESTA);
}
