/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for MEC1322 */

#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func)
{
	int i;
	uint32_t val;

	while (mask) {
		i = __builtin_ffs(mask) - 1;
		val = MEC1322_GPIO_CTL(port, i);
		val &= ~((1 << 12) | (1 << 13));
		val |= (func & 0x3) << 12;
		MEC1322_GPIO_CTL(port, i) = val;
		mask &= ~(1 << i);
	}
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	uint32_t mask = gpio_list[signal].mask;
	int i = 31 - __builtin_clz(mask);
	uint32_t val = MEC1322_GPIO_CTL(gpio_list[signal].port, i);

	if (val & (1 << 9)) /* Output */
		return (val & (1 << 16)) ? 1 : 0;
	else
		return (val & (1 << 24)) ? 1 : 0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	uint32_t mask = gpio_list[signal].mask;
	int i = 31 - __builtin_clz(mask);
	if (value)
		MEC1322_GPIO_CTL(gpio_list[signal].port, i) |= (1 << 16);
	else
		MEC1322_GPIO_CTL(gpio_list[signal].port, i) &= ~(1 << 16);
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	int i;
	uint32_t val;
	while (mask) {
		i = 31 - __builtin_clz(mask);
		mask &= ~(1 << i);
		val = MEC1322_GPIO_CTL(port, i);

		/*
		 * Select open drain first, so that we don't glitch the signal
		 * when changing the line to an output.
		 */
		if (flags & GPIO_OPEN_DRAIN)
			val |= (1 << 8);
		else
			val &= ~(1 << 8);

		if (flags & GPIO_OUTPUT) {
			val |= (1 << 9);
			val &= ~(1 << 10);
		} else {
			val &= ~(1 << 9);
			val |= (1 << 10);
		}

		/* Handle pullup / pulldown */
		if (flags & GPIO_PULL_UP)
			val = (val & ~0x3) | 0x1;
		else if (flags & GPIO_PULL_DOWN)
			val = (val & ~0x3) | 0x2;
		else
			val &= ~0x3;

		/* TODO(crosbug.com/p/24107): Set up interrupt */

		/* Use as GPIO */
		val &= ~((1 << 12) | (1 << 13));

		MEC1322_GPIO_CTL(port, i) = val;

		if (flags & GPIO_HIGH)
			MEC1322_GPIO_CTL(port, i) |= (1 << 16);
		else
			MEC1322_GPIO_CTL(port, i) &= ~(1 << 16);
	}
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	return EC_ERROR_UNIMPLEMENTED;
}

void gpio_pre_init(void)
{
	int i;
	const struct gpio_info *g = gpio_list;

	for (i = 0; i < GPIO_COUNT; i++, g++)
		gpio_set_flags_by_mask(g->port, g->mask, g->flags);
}
