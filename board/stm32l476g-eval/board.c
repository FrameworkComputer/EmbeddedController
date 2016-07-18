/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"

#ifdef CTS_MODULE
/*
 * Dummy interrupt handler. It's supposed to be overwritten by each suite
 * if needed.
 */
__attribute__((weak)) void cts_irq(enum gpio_signal signal)
{
}
#endif

#include "gpio_list.h"

void tick_event(void)
{
	static int count;

	gpio_set_level(GPIO_LED_GREEN, (count & 0x03) == 0);

	count++;
}
DECLARE_HOOK(HOOK_TICK, tick_event, HOOK_PRIO_DEFAULT);
