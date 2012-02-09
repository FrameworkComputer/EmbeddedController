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

int TaskAbc(void *data)
{
	char letter = (char)(unsigned)data;
	char string[2] = {letter, '\0' };
	task_id_t next = task_get_current() + 1;
	if (next > TASK_ID_TESTC)
		next = TASK_ID_TESTA;

	uart_printf("\n[starting Task %c]\n", letter);

	while (1) {
		uart_puts(string);
		uart_flush_output();
		task_send_msg(next, TASK_ID_CURRENT, 1);
	}

	return EC_SUCCESS;
}

int TaskTick(void *data)
{
	uart_set_console_mode(1);
	uart_printf("\n[starting Task T]\n");
	/* Print T every tick */
	while (1) {
		/* Wait for timer interrupt message */
		usleep(3000);
		uart_puts("T\n");
	}
}
