/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32G431 Nucleo-64 board-specific configuration */

#include "common.h"
#include "gpio.h"
#include "hooks.h"

#include "gpio_list.h" /* Must come after other header files. */


static void board_init(void)
{
	/*
	 * Using alt-function to send system clock to MCO pin (PA8). The alt
	 * function for module clock will not get configured unless the module
	 * is configured here.
	 */
	gpio_config_module(MODULE_CLOCK, 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void led_second(void)
{
	static int count;

	/* Blink user LED on nucleo board */
	gpio_set_level(GPIO_LED1, count++ & 0x1);
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);

