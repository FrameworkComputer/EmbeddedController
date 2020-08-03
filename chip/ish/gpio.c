/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for ISH */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define ISH_TOTAL_GPIO_PINS 8

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	/* Unimplemented GPIOs shouldn't do anything */
	if (g->port == UNIMPLEMENTED_GPIO_BANK)
		return 0;

	return  !!(ISH_GPIO_GPLR & g->mask);
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	const struct gpio_info *g = gpio_list + signal;

	/* Unimplemented GPIOs shouldn't do anything */
	if (g->port == UNIMPLEMENTED_GPIO_BANK)
		return;

	if (value)
		ISH_GPIO_GPSR |= g->mask;
	else
		ISH_GPIO_GPCR |= g->mask;
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	/* Unimplemented GPIOs shouldn't do anything */
	if (port == UNIMPLEMENTED_GPIO_BANK)
		return;

	/* ISH does not support level-trigger interrupts; only edge. */
	if (flags & (GPIO_INT_F_HIGH | GPIO_INT_F_LOW)) {
		ccprintf("\n\nISH does not support level trigger GPIO for %d "
			 "0x%02x!\n\n",
			 port, mask);
	}

	/* ISH 3 can't support both rising and falling edge */
	if (IS_ENABLED(CHIP_FAMILY_ISH3) &&
	    (flags & GPIO_INT_F_RISING) && (flags & GPIO_INT_F_FALLING)) {
		ccprintf("\n\nISH 2/3 does not support both rising & falling "
			 "edge for %d 0x%02x\n\n",
			 port, mask);
	}

	/* GPSR/GPCR Output high/low */
	if (flags & GPIO_HIGH) /* Output high */
		ISH_GPIO_GPSR |= mask;
	else if (flags & GPIO_LOW)  /* output low */
		ISH_GPIO_GPCR |= mask;

	/* GPDR pin direction 1 = output, 0 = input*/
	if (flags & GPIO_OUTPUT)
		ISH_GPIO_GPDR |= mask;
	else /* GPIO_INPUT or un-configured */
		ISH_GPIO_GPDR &= ~mask;

	/* Interrupt is asserted on rising edge */
	if (flags & GPIO_INT_F_RISING)
		ISH_GPIO_GRER |= mask;
	else
		ISH_GPIO_GRER &= ~mask;

	/* Interrupt is asserted on falling edge */
	if (flags & GPIO_INT_F_FALLING)
		ISH_GPIO_GFER |= mask;
	else
		ISH_GPIO_GFER &= ~mask;
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	/* Unimplemented GPIOs shouldn't do anything */
	if (g->port == UNIMPLEMENTED_GPIO_BANK)
		return EC_SUCCESS;

	ISH_GPIO_GIMR |= g->mask;
	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	ISH_GPIO_GIMR &= ~g->mask;
	return EC_SUCCESS;
}

int gpio_clear_pending_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	ISH_GPIO_GISR = g->mask;
	return EC_SUCCESS;
}

void gpio_pre_init(void)
{
	int i;
	int flags;
	int is_warm = system_is_reboot_warm();
	const struct gpio_info *g = gpio_list;

	for (i = 0; i < GPIO_COUNT; i++, g++) {

		flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		/*
		 * If this is a warm reboot, don't set the output levels
		 * or we'll shut off the AP.
		 */
		if (is_warm)
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}

	/* disable GPIO interrupts */
	ISH_GPIO_GIMR = 0;
	/* clear pending GPIO interrupts */
	ISH_GPIO_GISR = 0xFFFFFFFF;
}

static void gpio_init(void)
{
	task_enable_irq(ISH_GPIO_IRQ);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

static void gpio_interrupt(void)
{
	int i;
	const struct gpio_info *g = gpio_list;
	uint32_t gisr = ISH_GPIO_GISR;
	uint32_t gimr = ISH_GPIO_GIMR;

	/* mask off any not enabled pins */
	gisr &= gimr;

	for (i = 0; i < GPIO_IH_COUNT; i++, g++) {
		if (gisr & g->mask) {
			/* write 1 to clear interrupt status bit */
			ISH_GPIO_GISR = g->mask;
			gpio_irq_handlers[i](i);
		}
	}
}
DECLARE_IRQ(ISH_GPIO_IRQ, gpio_interrupt);
