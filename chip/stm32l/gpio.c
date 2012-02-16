/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "board.h"
#include "gpio.h"
#include "registers.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/* Signal information from board.c.  Must match order from enum gpio_signal. */
extern const struct gpio_info gpio_list[GPIO_COUNT];

/* For each EXTI bit, record which GPIO entry is using it */
static const struct gpio_info *exti_events[16];

int gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int i;

	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32L_RCC_AHBENR |= 0x3f;

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		/* bitmask for registers with 2 bits per GPIO pin */
		uint32_t mask2 = (g->mask * g->mask) | (g->mask * g->mask * 2);

		if (g->flags & GPIO_OUTPUT) {
			/* Set pin level */
			gpio_set_level(i, g->flags & GPIO_HIGH);
			/* General purpose output : MODE = 01 */
			STM32L_GPIO_MODER_OFF(g->port) |= 0x55555555 & mask2;
		} else {
			/* Input */
			STM32L_GPIO_MODER_OFF(g->port) &= ~mask2;
			if (g->flags & GPIO_PULL) {
				/* With pull up/down */
				if (g->flags & GPIO_HIGH) /* Pull Up = 01 */
					STM32L_GPIO_PUPDR_OFF(g->port) |=
							0x55555555 & mask2;
				else /* Pull Down = 10 */
					STM32L_GPIO_PUPDR_OFF(g->port) |=
							0xaaaaaaaa & mask2;
			}
		}

		/* Set up interrupts if necessary */
		ASSERT(!(g->flags & GPIO_INT_LEVEL));
		if (g->flags & (GPIO_INT_RISING | GPIO_INT_BOTH))
			STM32L_EXTI_RTSR |= g->mask;
		if (g->flags & (GPIO_INT_FALLING | GPIO_INT_BOTH))
			STM32L_EXTI_FTSR |= g->mask;
		/* Interrupt is enabled by gpio_enable_interrupt() */
	}

	/* Enable IRQs now that pins are set up */
	task_enable_irq(STM32L_IRQ_EXTI0);
	task_enable_irq(STM32L_IRQ_EXTI1);
	task_enable_irq(STM32L_IRQ_EXTI2);
	task_enable_irq(STM32L_IRQ_EXTI3);
	task_enable_irq(STM32L_IRQ_EXTI4);
	task_enable_irq(STM32L_IRQ_EXTI9_5);
	task_enable_irq(STM32L_IRQ_EXTI15_10);

	return EC_SUCCESS;
}


int gpio_get_level(enum gpio_signal signal)
{
	return !!(STM32L_GPIO_IDR_OFF(gpio_list[signal].port) &
		  gpio_list[signal].mask);
}


int gpio_set_level(enum gpio_signal signal, int value)
{
	STM32L_GPIO_BSRR_OFF(gpio_list[signal].port) =
			gpio_list[signal].mask << (value ? 0 : 16);

	return EC_SUCCESS;
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	uint32_t bit, group, shift, bank;

	/* Fail if not implemented or no interrupt handler */
	if (!g->mask || !g->irq_handler)
		return EC_ERROR_INVAL;

	bit = 31 - __builtin_clz(g->mask);

#ifdef CONFIG_DEBUG
	if (exti_events[bit]) {
		uart_printf("Overriding %s with %s on EXTI%d\n",
			    exti_events[bit]->name, g->name, bit);
	}
#endif
	exti_events[bit] = g;

	group = bit / 4;
	shift = (bit % 4) * 4;
	bank = (g->port - STM32L_GPIOA_BASE) / 0x400;
	STM32L_SYSCFG_EXTICR(group) = (STM32L_SYSCFG_EXTICR(group) &
			~(0xF << shift)) | (bank << shift);
	STM32L_EXTI_IMR |= g->mask;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Interrupt handler */

static void gpio_interrupt(void)
{
	int bit;
	const struct gpio_info *g;
	uint32_t pending = STM32L_EXTI_PR;

	STM32L_EXTI_PR = pending;

	while (pending) {
		bit = 31 - __builtin_clz(pending);
		g = exti_events[bit];
		if (g && g->irq_handler)
			g->irq_handler(g - gpio_list);
		pending &= ~(1 << bit);
	}
}
DECLARE_IRQ(STM32L_IRQ_EXTI0, gpio_interrupt, 1);
DECLARE_IRQ(STM32L_IRQ_EXTI1, gpio_interrupt, 1);
DECLARE_IRQ(STM32L_IRQ_EXTI2, gpio_interrupt, 1);
DECLARE_IRQ(STM32L_IRQ_EXTI3, gpio_interrupt, 1);
DECLARE_IRQ(STM32L_IRQ_EXTI4, gpio_interrupt, 1);
DECLARE_IRQ(STM32L_IRQ_EXTI9_5, gpio_interrupt, 1);
DECLARE_IRQ(STM32L_IRQ_EXTI15_10, gpio_interrupt, 1);
