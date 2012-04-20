/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "board.h"
#include "gpio.h"
#include "hooks.h"
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

	for (i = 0; i < GPIO_COUNT; i++, g++) {
		/* bitmask for registers with 2 bits per GPIO pin */
		uint32_t mask2 = (g->mask * g->mask) | (g->mask * g->mask * 2);
		uint32_t val;

		val = STM32L_GPIO_PUPDR_OFF(g->port) & ~mask2;
		if (g->flags & GPIO_PULL_UP) /* Pull Up = 01 */
			val |= 0x55555555 & mask2;
		else if (g->flags & GPIO_PULL_DOWN) /* Pull Down = 10 */
			val |= 0xaaaaaaaa & mask2;
		STM32L_GPIO_PUPDR_OFF(g->port) = val;

		if (g->flags & GPIO_OPEN_DRAIN)
			STM32L_GPIO_OTYPER_OFF(g->port) |= g->mask;

		/*
		 * Set pin level after port has been set up as to avoid
		 * potential damage, e.g. driving an open-drain output
		 * high before it has been configured as such.
		 */
		val = STM32L_GPIO_MODER_OFF(g->port) & ~mask2;
		if (g->flags & GPIO_OUTPUT) { /* General purpose, MODE = 01 */
			val |= 0x55555555 & mask2;
			STM32L_GPIO_MODER_OFF(g->port) = val;
			gpio_set_level(i, g->flags & GPIO_HIGH);
		} else if (g->flags & GPIO_INPUT) { /* Input, MODE=00 */
			STM32L_GPIO_MODER_OFF(g->port) = val;
		}

		/* Set up interrupts if necessary */
		ASSERT(!(g->flags & GPIO_INT_LEVEL));
		if (g->flags & (GPIO_INT_RISING | GPIO_INT_BOTH))
			STM32L_EXTI_RTSR |= g->mask;
		if (g->flags & (GPIO_INT_FALLING | GPIO_INT_BOTH))
			STM32L_EXTI_FTSR |= g->mask;
		/* Interrupt is enabled by gpio_enable_interrupt() */
	}

	return EC_SUCCESS;
}


static int gpio_init(void)
{
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
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);


void gpio_set_alternate_function(int port, int mask, int func)
{
	int bit;
	uint8_t half;
	uint32_t afr;
	uint32_t moder = STM32L_GPIO_MODER_OFF(port);

	/* Low half of the GPIO bank */
	half = mask & 0xff;
	afr = STM32L_GPIO_AFRL_OFF(port);
	while (half) {
		bit = 31 - __builtin_clz(half);
		afr &= ~(0xf << (bit * 4));
		afr |= func << (bit * 4);
		moder &= ~(0x3 << (bit * 2 + 0));
		moder |= 0x2 << (bit * 2 + 0);
		half &= ~(1 << bit);
	}
	STM32L_GPIO_AFRL_OFF(port) = afr;

	/* High half of the GPIO bank */
	half = mask >> 8;
	afr = STM32L_GPIO_AFRH_OFF(port);
	while (half) {
		bit = 31 - __builtin_clz(half);
		afr &= ~(0xf << (bit * 4));
		afr |= func << (bit * 4);
		moder &= ~(0x3 << (bit * 2 + 16));
		moder |= 0x2 << (bit * 2 + 16);
		half &= ~(1 << bit);
	}
	STM32L_GPIO_AFRH_OFF(port) = afr;
	STM32L_GPIO_MODER_OFF(port) = moder;
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
