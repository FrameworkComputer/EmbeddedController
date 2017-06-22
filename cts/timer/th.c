/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cts_common.h"
#include "gpio.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

/*
 * Interrupt handler
 *
 * DUT is supposed to trigger an interrupt when it's done counting down,
 * causing this function to be invoked.
 */
void cts_irq(enum gpio_signal signal)
{
	/* Wake up the CTS task */
	task_wake(TASK_ID_CTS);
}

static enum cts_rc timer_calibration_test(void)
{
	/* Error margin: +/-2 msec (0.2% for one second) */
	const int32_t margin = 2 * MSEC;
	int32_t elapsed, delta;
	timestamp_t t0, t1;

	gpio_enable_interrupt(GPIO_CTS_NOTIFY);
	interrupt_enable();

	sync();
	t0 = get_time();
	/* Wait for interrupt */
	task_wait_event(-1);
	t1 = get_time();

	elapsed = (int32_t)(t1.val - t0.val);
	delta = elapsed - SECOND;
	if (delta < -margin) {
		CPRINTS("DUT clock runs too fast: %+d usec", delta);
		return CTS_RC_FAILURE;
	}
	if (margin < delta) {
		CPRINTS("DUT clock runs too slow: %+d usec", delta);
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	cts_main_loop(tests, "Timer");
	task_wait_event(-1);
}
