/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/*
 * There's no way to trigger on both rising and falling edges, so force a
 * compiler error if we try. The workaround is to use the pinmux to connect
 * two GPIOs to the same input and configure each one for a separate edge.
 */
#undef GPIO_INT_BOTH
#define GPIO_INT_BOTH NOT_SUPPORTED_ON_CR50

#include "gpio_list.h"

/* Interrupt handler for button pushes */
void button_event(enum gpio_signal signal)
{
	int v;

	/* We have two GPIOs on the same input (one rising edge, one falling
	 * edge), so de-alias them */
	if (signal >= GPIO_SW_N_)
		signal -= (GPIO_SW_N_ - GPIO_SW_N);

	v = gpio_get_level(signal);
	ccprintf("Button %d = %d\n", signal, v);
	gpio_set_level(signal - GPIO_SW_N + GPIO_LED_4, v);
}

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_SW_N);
	gpio_enable_interrupt(GPIO_SW_S);
	gpio_enable_interrupt(GPIO_SW_W);
	gpio_enable_interrupt(GPIO_SW_E);
	gpio_enable_interrupt(GPIO_SW_N_);
	gpio_enable_interrupt(GPIO_SW_S_);
	gpio_enable_interrupt(GPIO_SW_W_);
	gpio_enable_interrupt(GPIO_SW_E_);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
