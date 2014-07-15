/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"


void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	uint32_t val = 0;
	uint32_t bit = 31 - __builtin_clz(mask);

	if (flags & GPIO_OUTPUT)
		val |= 1 << 0;
	else if (flags & GPIO_INPUT)
		val |= 0 << 0;

	if (flags & GPIO_PULL_DOWN)
		val |= 1 << 2;
	else if (flags & GPIO_PULL_UP)
		val |= 3 << 2;

	if (flags & GPIO_OUTPUT) {
		if (flags & GPIO_HIGH)
			NRF51_GPIO0_OUTSET = 1 << bit;
		else if (flags & GPIO_LOW)
			NRF51_GPIO0_OUTCLR = 1 << bit;
	}

	NRF51_PIN_CNF(bit) = val;
}


static void gpio_init(void)
{
	task_enable_irq(NRF51_PERID_GPIOTE);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);


test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return !!(NRF51_GPIO0_IN & gpio_list[signal].mask);
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	if (value)
		NRF51_GPIO0_OUTSET = gpio_list[signal].mask;
	else
		NRF51_GPIO0_OUTCLR = gpio_list[signal].mask;
}


void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int is_warm = 0;
	int i;

	if (NRF51_POWER_RESETREAS & (1 << 2)) {
		/* This is a warm reboot */
		is_warm = 1;
	}

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		int flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		/*
		 * If this is a warm reboot, don't set the output levels or
		 * we'll shut off the AP.
		 */
		if (is_warm)
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		/* Set up GPIO based on flags */
		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}
}


/*
 *  TODO: implement GPIO interrupt.
 */
int gpio_enable_interrupt(enum gpio_signal signal)
{
	return EC_ERROR_INVAL;
}
void gpio_interrupt(void)
{
}
DECLARE_IRQ(NRF51_PERID_GPIOTE, gpio_interrupt, 1);
