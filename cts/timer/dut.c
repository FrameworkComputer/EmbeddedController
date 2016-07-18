/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "dut_common.h"
#include "gpio.h"
#include "timer.h"
#include "watchdog.h"

static enum cts_rc timer_calibration_test(void)
{
	gpio_set_flags(GPIO_OUTPUT_TEST, GPIO_ODR_HIGH);

	sync();
	usleep(SECOND);
	gpio_set_level(GPIO_OUTPUT_TEST, 0);

	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	enum cts_rc rc;
	int i;

	for (i = 0; i < CTS_TEST_ID_COUNT; i++) {
		sync();
		rc = tests[i].run();
		CPRINTF("\n%s %d\n", tests[i].name, rc);
		cflush();
	}

	CPRINTS("Timer test suite finished");
	cflush();

	while (1) {
		watchdog_reload();
		sleep(1);
	}
}
