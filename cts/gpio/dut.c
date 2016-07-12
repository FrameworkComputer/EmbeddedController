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

enum cts_error_code sync_test(void)
{
	return CTS_SUCCESS;
}

enum cts_error_code set_high_test(void)
{
	gpio_set_flags(GPIO_OUTPUT_TEST, GPIO_ODR_LOW);
	gpio_set_level(GPIO_OUTPUT_TEST, 1);
	msleep(READ_WAIT_TIME_MS*2);
	return CTS_ERROR_UNKNOWN;
}

enum cts_error_code set_low_test(void)
{
	gpio_set_flags(GPIO_OUTPUT_TEST, GPIO_ODR_LOW);
	gpio_set_level(GPIO_OUTPUT_TEST, 0);
	msleep(READ_WAIT_TIME_MS*2);
	return CTS_ERROR_UNKNOWN;
}

enum cts_error_code read_high_test(void)
{
	int level;

	gpio_set_flags(GPIO_INPUT_TEST, GPIO_INPUT | GPIO_PULL_UP);
	msleep(READ_WAIT_TIME_MS);
	level = gpio_get_level(GPIO_INPUT_TEST);
	if (level)
		return CTS_SUCCESS;
	else
		return CTS_ERROR_FAILURE;
}

enum cts_error_code read_low_test(void)
{
	int level;

	gpio_set_flags(GPIO_INPUT_TEST, GPIO_INPUT | GPIO_PULL_UP);
	msleep(READ_WAIT_TIME_MS);
	level = gpio_get_level(GPIO_INPUT_TEST);
	if (!level)
		return CTS_SUCCESS;
	else
		return CTS_ERROR_FAILURE;
}

enum cts_error_code od_read_high_test(void)
{
	int level;

	gpio_set_flags(GPIO_OUTPUT_TEST, GPIO_ODR_HIGH | GPIO_PULL_UP);
	msleep(READ_WAIT_TIME_MS);
	level = gpio_get_level(GPIO_OUTPUT_TEST);
	if (!level)
		return CTS_SUCCESS;
	else
		return CTS_ERROR_FAILURE;
}

#include "cts_testlist.h"

void cts_task(void)
{
	enum cts_error_code results[CTS_TEST_ID_COUNT];
	int i;

	for (i = 0; i < CTS_TEST_ID_COUNT; i++) {
		sync();
		results[i] = tests[i].run();
	}

	CPRINTS("GPIO test suite finished");
	uart_flush_output();
	CPRINTS("Results:");
	for (i = 0; i < CTS_TEST_ID_COUNT; i++) {
		switch (results[i]) {
		case CTS_SUCCESS:
			CPRINTS("%s) Passed", tests[i].name);
			break;
		case CTS_ERROR_FAILURE:
			CPRINTS("%s) Failed", tests[i].name);
			break;
		case CTS_ERROR_BAD_SYNC:
			CPRINTS("%s) Bad sync", tests[i].name);
			break;
		case CTS_ERROR_UNKNOWN:
			CPRINTS("%s) Test result unknown", tests[i].name);
			break;
		default:
			CPRINTS("%s) ErrorCode (%d) not recognized",
				tests[i].name, results[i]);
			break;
		}
	}

	while (1) {
		watchdog_reload();
		sleep(1);
	}
}
