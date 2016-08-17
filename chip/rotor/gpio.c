/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Rotor MCU */

#include "gpio.h"
#include "registers.h"
#include "task.h"
#include "util.h"

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	uint32_t mask = gpio_list[signal].mask;
	uint32_t val;

	if (mask == 0)
		return 0;

	val = ROTOR_MCU_GPIO_PLR(gpio_list[signal].port);
	return (val & mask) ? 1 : 0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	uint32_t mask = gpio_list[signal].mask;

	if (mask == 0)
		return;

	/* Enable direct writes to take effect. */
	ROTOR_MCU_GPIO_DWER(gpio_list[signal].port) |= mask;

	if (value)
		ROTOR_MCU_GPIO_OLR(gpio_list[signal].port) |= mask;
	else
		ROTOR_MCU_GPIO_OLR(gpio_list[signal].port) &= ~mask;
}

void gpio_pre_init(void)
{
	int i;
	const struct gpio_info *g = gpio_list;

	/* Set all GPIOs to their defaults. */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		int flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	while (mask) {
		int i = GPIO_MASK_TO_NUM(mask);
		const uint32_t this_mask = (1 << i);
		mask &= ~this_mask;

		/* Enable direct writes to take effect. */
		ROTOR_MCU_GPIO_DWER(port) |= this_mask;

		/* Input/Output */
		if (flags & GPIO_OUTPUT)
			ROTOR_MCU_GPIO_PDR(port) |= this_mask;
		else
			ROTOR_MCU_GPIO_PDR(port) &= ~this_mask;

		/* Pull Up / Pull Down */
		if (flags & GPIO_PULL_UP) {
			ROTOR_MCU_GPIO_PCFG(port, i) |= (1 << 14);
		} else if (flags & GPIO_PULL_DOWN) {
			ROTOR_MCU_GPIO_PCFG(port, i) |= (1 << 13);
		} else {
			/* No pull up/down */
			ROTOR_MCU_GPIO_PCFG(port, i) &= ~(3 << 14);
		}

		/* Edge vs. Level Interrupts */
		if (flags & (GPIO_INT_F_RISING | GPIO_INT_F_FALLING))
			ROTOR_MCU_GPIO_IMR(port) &= this_mask;
		else
			ROTOR_MCU_GPIO_IMR(port) |= this_mask;

		/* Interrupt polarity */
		if (flags & (GPIO_INT_F_RISING | GPIO_INT_F_HIGH))
			ROTOR_MCU_GPIO_HRIPR(port) |= this_mask;
		else
			ROTOR_MCU_GPIO_HRIPR(port) &= ~this_mask;

		if (flags & (GPIO_INT_F_FALLING | GPIO_INT_F_LOW))
			ROTOR_MCU_GPIO_LFIPR(port) |= this_mask;
		else
			ROTOR_MCU_GPIO_LFIPR(port) &= this_mask;

		/* Set level */
		if (flags & GPIO_OUTPUT) {
			if (flags & GPIO_HIGH)
				ROTOR_MCU_GPIO_OLR(port) |= this_mask;
			else if (flags & GPIO_LOW)
				ROTOR_MCU_GPIO_OLR(port) &= ~this_mask;
		}

		/* No analog support. */
	};
}

void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func)
{
	int i;

	while (mask) {
		i = GPIO_MASK_TO_NUM(mask);
		mask &= ~(1 << i);

		ROTOR_MCU_GPIO_PCFG(port, i) &= ~0x7;
		if (func > 0)
			ROTOR_MCU_GPIO_PCFG(port, i) |= (func & 0x7);
	};
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	if ((g->mask == 0) || (signal >= GPIO_IH_COUNT))
		return EC_ERROR_UNKNOWN;

	ROTOR_MCU_GPIO_ITER(g->port) |= g->mask;

	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	if ((g->mask == 0) || (signal >= GPIO_IH_COUNT))
		return EC_ERROR_UNKNOWN;

	ROTOR_MCU_GPIO_ITER(g->port) &= ~g->mask;

	return EC_SUCCESS;
}

/**
 *  GPIO IRQ handler.
 *
 * @param port		GPIO port (GPIO_[ABCDE])
 * @param int_status	interrupt status for the port.
*/
static void gpio_interrupt(int port, uint32_t int_status)
{
	int i = 0;
	const struct gpio_info *g = gpio_list;

	/* Search for the right IRQ handler. */
	for (i = 0; i < GPIO_IH_COUNT && int_status; i++, g++) {
		if (port == g->port && (int_status & g->mask)) {
			gpio_irq_handlers[i](i);
			int_status &= ~g->mask;
		}
	}
}

/**
 * Handlers for each GPIO port.  These read and clear the interrupt bits for
 * the port, then call the master handler above.
 */
#define GPIO_IRQ_FUNC(irqfunc, port)				\
	void irqfunc(void)					\
	{							\
		uint32_t int_status = ROTOR_MCU_GPIO_ISR(port);	\
		ROTOR_MCU_GPIO_ISR(port) = int_status;		\
		gpio_interrupt(port, int_status);		\
	}

GPIO_IRQ_FUNC(__gpio_a_interrupt, GPIO_A);
GPIO_IRQ_FUNC(__gpio_b_interrupt, GPIO_B);
GPIO_IRQ_FUNC(__gpio_c_interrupt, GPIO_C);
GPIO_IRQ_FUNC(__gpio_d_interrupt, GPIO_D);
GPIO_IRQ_FUNC(__gpio_e_interrupt, GPIO_E);

/* Declare per-bank GPIO IRQs. */
DECLARE_IRQ(ROTOR_MCU_IRQ_GPIO_0, __gpio_a_interrupt, 1);
DECLARE_IRQ(ROTOR_MCU_IRQ_GPIO_1, __gpio_b_interrupt, 1);
DECLARE_IRQ(ROTOR_MCU_IRQ_GPIO_2, __gpio_c_interrupt, 1);
DECLARE_IRQ(ROTOR_MCU_IRQ_GPIO_3, __gpio_d_interrupt, 1);
DECLARE_IRQ(ROTOR_MCU_IRQ_GPIO_4, __gpio_e_interrupt, 1);
