/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 * Copyright 2011 Google Inc.
 *
 * Tasks for timer test.
 */

#include "common.h"
#include "uart.h"
#include "task.h"
#include "timer.h"

/* Linear congruential pseudo random number generator*/
static uint32_t prng(uint32_t x)
{
	return 22695477 * x + 1;
}

/* period between 500us and 128ms */
#define PERIOD_US(num) (((num % 256) + 1) * 500)

int TaskTimer(void *seed)
{
	uint32_t num = (uint32_t)seed;
	task_id_t id = task_get_current();

	uart_printf("\n[Timer task %d]\n", id);

	while (1) {
		/* Wait for a "random" period */
		task_wait_msg(PERIOD_US(num));
		uart_printf("%01d\n", id);
		/* next pseudo random delay */
		num = prng(num);
	}

	return EC_SUCCESS;
}
