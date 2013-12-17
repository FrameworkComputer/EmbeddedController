/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tasks for timer test.
 */

#include "common.h"
#include "console.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Linear congruential pseudo random number generator*/
static uint32_t prng(uint32_t x)
{
	return 22695477 * x + 1;
}

/* period between 500us and 128ms */
#define PERIOD_US(num) (((num % 256) + 1) * 500)

#define TEST_TIME (3 * SECOND)

int task_timer(void *seed)
{
	uint32_t num = (uint32_t)seed;
	task_id_t id = task_get_current();
	timestamp_t start;

	while (1) {
		task_wait_event(-1);

		ccprintf("\n[Timer task %d]\n", id);
		start = get_time();

		while (get_time().val - start.val < TEST_TIME) {
			/* Wait for a "random" period */
			task_wait_event(PERIOD_US(num));
			ccprintf("%01d\n", id);
			/* next pseudo random delay */
			num = prng(num);
		}
	}

	return EC_SUCCESS;
}

void run_test(void)
{
	task_wake(TASK_ID_TMRD);
	task_wake(TASK_ID_TMRC);
	task_wake(TASK_ID_TMRB);
	task_wake(TASK_ID_TMRA);
}
