/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 * Copyright 2011 Google Inc.
 *
 * Tasks for mutexes basic tests.
 */

#include "common.h"
#include "uart.h"
#include "task.h"
#include "timer.h"

static struct mutex mtx;

/* Linear congruential pseudo random number generator*/
static uint32_t prng(uint32_t x)
{
	return 22695477 * x + 1;
}

/* period between 50us and 12.8ms */
#define PERIOD_US(num) (((num % 256) + 1) * 50)
/* one of the 3 MTX3x tasks */
#define RANDOM_TASK(num) (TASK_ID_MTX3C + (num % 3))

int mutex_random_task(void *unused)
{
	char letter = 'A'+(TASK_ID_MTX3A - task_get_current());
	/* wait to be activated */

	while (1) {
		task_wait_event(0);
		uart_printf("%c+\n", letter);
		mutex_lock(&mtx);
		uart_printf("%c=\n", letter);
		task_wait_event(0);
		uart_printf("%c-\n", letter);
		mutex_unlock(&mtx);
	}

	task_wait_event(0);

	return EC_SUCCESS;
}

int mutex_second_task(void *unused)
{
	task_id_t id = task_get_current();

	uart_printf("\n[Mutex second task %d]\n", id);

	task_wait_event(0);
	uart_printf("MTX2: locking...");
	mutex_lock(&mtx);
	uart_printf("done\n");
	task_wake(TASK_ID_MTX1);
	uart_printf("MTX2: unlocking...\n");
	mutex_unlock(&mtx);

	task_wait_event(0);

	return EC_SUCCESS;
}

int mutex_main_task(void *unused)
{
	task_id_t id = task_get_current();
	uint32_t rdelay = (uint32_t)0x0bad1dea;
	uint32_t rtask = (uint32_t)0x1a4e1dea;
	int i;

	uart_printf("\n[Mutex main task %d]\n", id);

	/* --- Lock/Unlock without contention --- */
	uart_printf("No contention :");
	mutex_lock(&mtx);
	mutex_unlock(&mtx);
	mutex_lock(&mtx);
	mutex_unlock(&mtx);
	mutex_lock(&mtx);
	mutex_unlock(&mtx);
	uart_printf("done.\n");

	/* --- Serialization to test simple contention --- */
	uart_printf("Simple contention :\n");
	/* lock the mutex from the other task */
	task_set_event(TASK_ID_MTX2, TASK_EVENT_WAKE, 1);
	/* block on the mutex */
	uart_printf("MTX1: blocking...\n");
	mutex_lock(&mtx);
	uart_printf("MTX1: get lock\n");
	mutex_unlock(&mtx);

	/* --- mass lock-unlocking from several tasks --- */
	uart_printf("Massive locking/unlocking :\n");
	for (i = 0; i < 500; i++) {
		/* Wake up a random task */
		task_wake(RANDOM_TASK(rtask));
		/* next pseudo random delay */
		rtask = prng(rtask);
		/* Wait for a "random" period */
		task_wait_event(PERIOD_US(rdelay));
		/* next pseudo random delay */
		rdelay = prng(rdelay);
	}

	uart_printf("Test done.\n");
	task_wait_event(0);

	return EC_SUCCESS;
}
