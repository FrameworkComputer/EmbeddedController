/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for emulator */

#include "console.h"

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "timer.h"
#include "util.h"

static int gpio_values[GPIO_COUNT];
static int gpio_interrupt_enabled[GPIO_COUNT];

/* Create a dictionary of names for debug console print */
#define GPIO_INT(name, pin, flags, signal) #name,
#define GPIO(name, pin, flags) #name,
const char * gpio_names[GPIO_COUNT] = {
	#include "gpio.wrap"
};
#undef GPIO
#undef GPIO_INT

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
	void (*ih)(enum gpio_signal signal);

	gpio_values[signal] = value;

	ccprints("Setting GPIO_%s to %d", gpio_names[signal], value);

	if (signal >= GPIO_IH_COUNT || !gpio_interrupt_enabled[signal])
		return;

	ih = gpio_irq_handlers[signal];

	if (gpio_interrupt_check(flags, old_value, value))
		ih(signal);
}

test_mockable int gpio_enable_interrupt(enum gpio_signal signal)
{
	gpio_interrupt_enabled[signal] = 1;
	return EC_SUCCESS;
}

test_mockable int gpio_disable_interrupt(enum gpio_signal signal)
{
	gpio_interrupt_enabled[signal] = 0;
	return EC_SUCCESS;
}

test_mockable int gpio_clear_pending_interrupt(enum gpio_signal signal)
{
	return EC_SUCCESS;
}

test_mockable void gpio_set_flags_by_mask(uint32_t port, uint32_t mask,
					  uint32_t flags)
{
	/* Nothing */
}

test_mockable void gpio_set_alternate_function(uint32_t port, uint32_t mask,
						enum gpio_alternate_func func)
{
	/* Nothing */
}
