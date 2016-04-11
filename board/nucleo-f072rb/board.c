/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"

void button_event(enum gpio_signal signal)
{
	gpio_set_level(GPIO_LED_U, 1);
}

#include "gpio_list.h"

void tick_event(void)
{
	static int count;

	gpio_set_level(GPIO_LED_U, (count & 0x07) == 0);

	count++;
}
DECLARE_HOOK(HOOK_TICK, tick_event, HOOK_PRIO_DEFAULT);

/******************************************************************************
 * Initialize board.
 */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_USER_BUTTON);

}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
