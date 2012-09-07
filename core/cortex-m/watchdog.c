/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog common code */

#include "board.h"
#include "common.h"
#include "config.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "watchdog.h"


void watchdog_trace(uint32_t excep_lr, uint32_t excep_sp)
{
	uint32_t psp;
	uint32_t *stack;

	asm("mrs %0, psp" : "=r"(psp));
	if ((excep_lr & 0xf) == 1) {
		/* we were already in exception context */
		stack = (uint32_t *)excep_sp;
	} else {
		/* we were in task context */
		stack = (uint32_t *)psp;
	}

	uart_printf("### WATCHDOG PC=%08x / LR=%08x / pSP=%08x ",
		    stack[6], stack[5], psp);
	if ((excep_lr & 0xf) == 1)
		uart_puts("(exc) ###\n");
	else
		uart_printf("(task %d) ###\n", task_get_current());
	/* Ensure this debug message is always flushed to the UART */
	uart_emergency_flush();

	/* If we are blocked in a high priority IT handler, the following debug
	 * messages might not appear but they are useless in that situation. */
	timer_print_info();
	uart_emergency_flush();
	task_print_list();
	uart_emergency_flush();
}


/* Low priority task to reload the watchdog */
void watchdog_task(void)
{
	while (1) {
		usleep(WATCHDOG_RELOAD_MS * 1000);
		watchdog_reload();
	}
}
