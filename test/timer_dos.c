/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tasks for timer test.
 */

#include "common.h"
#include "console.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

/* period between 500us and 128ms */
#define PERIOD_US(num) (((num % 256) + 1) * 500)

#define TEST_TIME (3 * SECOND)

#define ERROR_MARGIN 5

static int calculate_golden(uint32_t seed)
{
	int golden = 0;
	uint32_t elapsed = 0;
	while (1) {
		elapsed += PERIOD_US(seed);
		++golden;
		if (elapsed >= TEST_TIME)
			return golden;
		seed = prng(seed);
	}
}

int task_timer(void *seed)
{
	uint32_t num = (uint32_t)(uintptr_t)seed;
	int golden_cnt = calculate_golden(num);
	task_id_t id = task_get_current();
	timestamp_t start;
	int cnt = 0;

	while (1) {
		task_wait_event(-1);

		ccprintf("\n[Timer task %d]\n", id);
		start = get_time();

		while (get_time().val - start.val < TEST_TIME) {
			/* Wait for a "random" period */
			task_wait_event(PERIOD_US(num));
			ccprintf("%01d\n", id);
			cnt++;
			/* next pseudo random delay */
			num = prng(num);
		}
		ccprintf("Task %d: Count=%d Golden=%d\n", id, cnt, golden_cnt);
		cnt -= golden_cnt;
		if (cnt < 0)
			cnt = -cnt;
		if (cnt > ERROR_MARGIN) {
			ccprintf("Count differs from Golden by more than %d!\n",
				 ERROR_MARGIN);
			test_fail();
		}
	}

	return EC_SUCCESS;
}

void run_test(void)
{
	wait_for_task_started();
	task_wake(TASK_ID_TMRD);
	task_wake(TASK_ID_TMRC);
	task_wake(TASK_ID_TMRB);
	task_wake(TASK_ID_TMRA);
	usleep(TEST_TIME + SECOND);
	test_pass();
}
