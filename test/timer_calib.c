/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 * Copyright 2011 Google Inc.
 *
 * Tasks for scheduling test.
 */

#include "common.h"
#include "uart.h"
#include "task.h"
#include "timer.h"

uint32_t difftime(timestamp_t t0, timestamp_t t1)
{
	return (uint32_t)(t1.val-t0.val);
}

int timer_calib_task(void *data)
{
	timestamp_t t0, t1;
	unsigned d;

	uart_printf("\n=== Timer calibration ===\n");

	t0 = get_time();
	t1 = get_time();
	uart_printf("- back-to-back get_time : %d us\n", difftime(t0, t1));

	/* Sleep for 5 seconds */
	uart_printf("- sleep 1s :\n  ");
	uart_flush_output();
	uart_printf("Go...");
	t0 = get_time();
	usleep(1000000);
	t1 = get_time();
	uart_printf("done. delay = %d us\n", difftime(t0, t1));
	
	/* try small usleep */
	uart_printf("- short sleep :\n");
	uart_flush_output();
	for (d=128 ; d > 0; d = d / 2) {
		t0 = get_time();
		usleep(d);
		t1 = get_time();
		uart_printf("  %d us => %d us\n", d, difftime(t0, t1));
		uart_flush_output();
	}

	uart_printf("Done.\n");
	/* sleep forever */
	task_wait_msg(-1);

	return EC_SUCCESS;
}
