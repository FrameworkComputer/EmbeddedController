/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tasks for scheduling test.
 */

#include "common.h"
#include "console.h"
#include "task.h"
#include "timer.h"
#include "util.h"

uint32_t difftime(timestamp_t t0, timestamp_t t1)
{
	return (uint32_t)(t1.val - t0.val);
}

int timer_calib_task(void *data)
{
	timestamp_t t0, t1;
	unsigned int d;

	while (1) {
		task_wait_event(-1);

		ccprintf("\n=== Timer calibration ===\n");

		t0 = get_time();
		t1 = get_time();
		ccprintf("- back-to-back get_time : %d us\n", difftime(t0, t1));

		/* Sleep for 5 seconds */
		ccprintf("- sleep 1s :\n  ");
		cflush();
		ccprintf("Go...");
		t0 = get_time();
		crec_usleep(1000000);
		t1 = get_time();
		ccprintf("done. delay = %d us\n", difftime(t0, t1));

		/* try small usleep */
		ccprintf("- short sleep :\n");
		cflush();
		for (d = 128; d > 0; d = d / 2) {
			t0 = get_time();
			crec_usleep(d);
			t1 = get_time();
			ccprintf("  %d us => %d us\n", d, difftime(t0, t1));
			cflush();
		}

		ccprintf("Done.\n");
	}

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	task_wake(TASK_ID_TESTTMR);
}
