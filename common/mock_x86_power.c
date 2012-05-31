/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock X86 chipset power control module for Chrome EC */

#include "chipset.h"
#include "lpc.h"
#include "timer.h"
#include "uart.h"
#include "x86_power.h"

void x86_power_cpu_overheated(int too_hot)
{
	/* Print transitions */
	static int last_val = 0;
	if (too_hot != last_val) {
		if (too_hot)
			uart_printf("CPU overheated.\n");
		else
			uart_printf("CPU no longer overheated.\n");
		last_val = too_hot;
	}
}


void x86_power_force_shutdown(void)
{
	uart_puts("Force shutdown\n");
}


void chipset_throttle_cpu(int throttle)
{
	/* Print transitions */
	static int last_val = 0;
	if (throttle != last_val) {
		if (throttle)
			uart_printf("Throttle CPU.\n");
		else
			uart_printf("No longer throttle CPU.\n");
		last_val = throttle;
	}
}


void x86_power_interrupt(enum gpio_signal signal)
{
	/* Not implemented */
	return;
}


void x86_power_task(void)
{
	/* Do nothing */
	while (1)
		usleep(5000000);
}
