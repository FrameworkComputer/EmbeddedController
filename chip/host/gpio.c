/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for emulator */

#include "console.h"

#include "common.h"
#include "gpio.h"
#include "timer.h"
#include "util.h"

static int gpio_values[GPIO_COUNT];
static int gpio_interrupt_enabled[GPIO_COUNT];

test_mockable void gpio_pre_init(void)
{
	/* Nothing */
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return gpio_values[signal];
}

static int gpio_interrupt_check(uint32_t flags, int old, int new)
{
	if ((flags & GPIO_INT_F_RISING) && old == 0 && new == 1)
		return 1;
	if ((flags & GPIO_INT_F_FALLING) && old == 1 && new == 0)
		return 1;
	if ((flags & GPIO_INT_F_LOW) && new == 0)
		return 1;
	if ((flags & GPIO_INT_F_HIGH) && new == 1)
		return 1;
	return 0;
}

test_mockable void gpio_set_level(enum gpio_signal signal, int value)
{
	const struct gpio_info *g = gpio_list + signal;
	const uint32_t flags = g->flags;
	const int old_value = gpio_values[signal];
	void (*ih)(enum gpio_signal signal) = gpio_irq_handlers[signal];

	gpio_values[signal] = value;

	if (signal >= GPIO_IH_COUNT || !gpio_interrupt_enabled[signal])
		return;

	if (gpio_interrupt_check(flags, old_value, value))
		ih(signal);
}

test_mockable int gpio_enable_interrupt(enum gpio_signal signal)
{
	gpio_interrupt_enabled[signal] = 1;
	return EC_SUCCESS;
}

test_mockable void gpio_set_flags_by_mask(uint32_t port, uint32_t mask,
					  uint32_t flags)
{
	/* Nothing */
}

test_mockable void gpio_set_alternate_function(uint32_t port, uint32_t mask,
					       int func)
{
	/* Nothing */
}
