/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cts_common.h"
#include "gpio.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

static enum cts_rc timer_calibration_test(void)
{
	gpio_set_flags(GPIO_OUTPUT_TEST, GPIO_ODR_HIGH);

	sync();
	crec_sleep(1);
	gpio_set_level(GPIO_OUTPUT_TEST, 0);

	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	cts_main_loop(tests, "Timer");
	task_wait_event(-1);
}
