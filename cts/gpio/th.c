/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "watchdog.h"
#include "uart.h"
#include "timer.h"
#include "watchdog.h"
#include "dut_common.h"
#include "cts_common.h"
#include "gpio_common.h"

enum cts_error_code sync_test(void)
{
	return SUCCESS;
}

enum cts_error_code set_high_test(void)
{
	int level;

	gpio_set_flags(GPIO_INPUT_TEST, GPIO_INPUT | GPIO_PULL_UP);
	msleep(READ_WAIT_TIME_MS);
	level = gpio_get_level(GPIO_INPUT_TEST);
	if (level)
		return SUCCESS;
	else
		return FAILURE;
}

enum cts_error_code set_low_test(void)
{
	int level;

	gpio_set_flags(GPIO_INPUT_TEST, GPIO_INPUT | GPIO_PULL_UP);
	msleep(READ_WAIT_TIME_MS);
	level = gpio_get_level(GPIO_INPUT_TEST);
	if (!level)
		return SUCCESS;
	else
		return FAILURE;
}

enum cts_error_code read_high_test(void)
{
	gpio_set_flags(GPIO_OUTPUT_TEST, GPIO_ODR_LOW);
	gpio_set_level(GPIO_OUTPUT_TEST, 1);
	msleep(READ_WAIT_TIME_MS*2);
	return UNKNOWN;
}

enum cts_error_code read_low_test(void)
{
	gpio_set_flags(GPIO_OUTPUT_TEST, GPIO_ODR_LOW);
	gpio_set_level(GPIO_OUTPUT_TEST, 0);
	msleep(READ_WAIT_TIME_MS*2);
	return UNKNOWN;
}

enum cts_error_code od_read_high_test(void)
{
	gpio_set_flags(GPIO_INPUT_TEST, GPIO_OUTPUT | GPIO_ODR_LOW);
	msleep(READ_WAIT_TIME_MS*2);
	return UNKNOWN;
}

void cts_task(void)
{
	enum cts_error_code results[GPIO_CTS_TEST_COUNT];
	int i;

	/* Don't bother checking sync's return value now because
	 * host will deal with hanging syncs/tests as well as
	 * interpreting test results later
	 */
	sync();
	results[0] = sync_test();
	sync();
	results[1] = set_low_test();
	sync();
	results[2] = set_high_test();
	sync();
	results[3] = read_high_test();
	sync();
	results[4] = read_low_test();
	sync();
	results[5] = od_read_high_test();

	CPRINTS("GPIO test suite finished");
	uart_flush_output();
	CPRINTS("Results:");
	for (i = 0; i < GPIO_CTS_TEST_COUNT; i++) {
		switch (results[i]) {
		case SUCCESS:
			CPRINTS("%d) Passed", i);
			break;
		case FAILURE:
			CPRINTS("%d) Failed", i);
			break;
		case BAD_SYNC:
			CPRINTS("%d) Bad sync", i);
			break;
		case UNKNOWN:
			CPRINTS("%d) Test result unknown", i);
			break;
		default:
			CPRINTS("%d) ErrorCode not recognized", i);
			break;
		}
	}

	while (1) {
		watchdog_reload();
		sleep(1);
	}
}
