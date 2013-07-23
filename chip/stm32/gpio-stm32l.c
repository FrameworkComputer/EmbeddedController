/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_GPIO, outstr)
#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)

/* For each EXTI bit, record which GPIO entry is using it */
static const struct gpio_info *exti_events[16];

void gpio_set_flags(enum gpio_signal signal, int flags)
{
	const struct gpio_info *g = gpio_list + signal;

	/* Bitmask for registers with 2 bits per GPIO pin */
	const uint32_t mask2 = (g->mask * g->mask) | (g->mask * g->mask * 2);
	uint32_t val;

	/* Set up pullup / pulldown */
	val = STM32_GPIO_PUPDR(g->port) & ~mask2;
	if (flags & GPIO_PULL_UP)
		val |= 0x55555555 & mask2;	/* Pull Up = 01 */
	else if (flags & GPIO_PULL_DOWN)
		val |= 0xaaaaaaaa & mask2;	/* Pull Down = 10 */
	STM32_GPIO_PUPDR(g->port) = val;

	/*
	 * Select open drain first, so that we don't glitch the signal when
	 * changing the line to an output.
	 */
	if (flags & GPIO_OPEN_DRAIN)
		STM32_GPIO_OTYPER(g->port) |= g->mask;

	val = STM32_GPIO_MODER(g->port) & ~mask2;
	if (flags & GPIO_OUTPUT) {
		/*
		 * Set pin level first to avoid glitching.  This is harmless on
		 * STM32L because the set/reset register isn't connected to the
		 * output drivers until the pin is made an output.
		 */
		if (flags & GPIO_HIGH)
			gpio_set_level(signal, 1);
		else if (flags & GPIO_LOW)
			gpio_set_level(signal, 0);

		/* General purpose, MODE = 01 */
		val |= 0x55555555 & mask2;
		STM32_GPIO_MODER(g->port) = val;

	} else if (flags & GPIO_INPUT) {
		/* Input, MODE=00 */
		STM32_GPIO_MODER(g->port) = val;
	}

	/* Set up interrupts if necessary */
	ASSERT(!(flags & GPIO_INT_LEVEL));
	if (flags & (GPIO_INT_RISING | GPIO_INT_BOTH))
		STM32_EXTI_RTSR |= g->mask;
	if (flags & (GPIO_INT_FALLING | GPIO_INT_BOTH))
		STM32_EXTI_FTSR |= g->mask;
	/* Interrupt is enabled by gpio_enable_interrupt() */
}

void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int is_warm = 0;
	int i;

	/* Required to configure external IRQ lines (SYSCFG_EXTICRn) */
	/* FIXME: This seems to break USB download in U-Boot (?!?) */
	STM32_RCC_APB2ENR |= 1 << 0;

	if ((STM32_RCC_AHBENR & 0x3f) == 0x3f) {
		/* This is a warm reboot */
		is_warm = 1;
	} else {
		/*
		 * Enable all GPIOs clocks
		 * TODO: more fine-grained enabling for power saving
		 */
		STM32_RCC_AHBENR |= 0x3f;
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
		gpio_set_flags(i, flags);
	}
}

static void gpio_init(void)
{
	/* Enable IRQs now that pins are set up */
	task_enable_irq(STM32_IRQ_EXTI0);
	task_enable_irq(STM32_IRQ_EXTI1);
	task_enable_irq(STM32_IRQ_EXTI2);
	task_enable_irq(STM32_IRQ_EXTI3);
	task_enable_irq(STM32_IRQ_EXTI4);
	task_enable_irq(STM32_IRQ_EXTI9_5);
	task_enable_irq(STM32_IRQ_EXTI15_10);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

void gpio_set_alternate_function(int port, int mask, int func)
{
	int bit;
	uint8_t half;
	uint32_t afr;
	uint32_t moder = STM32_GPIO_MODER(port);

	if (func < 0) {
		/* Return to normal GPIO function, defaulting to input. */
		while (mask) {
			bit = 31 - __builtin_clz(mask);
			moder &= ~(0x3 << (bit * 2));
			mask &= ~(1 << bit);
		}
		STM32_GPIO_MODER(port) = moder;
		return;
	}

	/* Low half of the GPIO bank */
	half = mask & 0xff;
	afr = STM32_GPIO_AFRL(port);
	while (half) {
		bit = 31 - __builtin_clz(half);
		afr &= ~(0xf << (bit * 4));
		afr |= func << (bit * 4);
		moder &= ~(0x3 << (bit * 2 + 0));
		moder |= 0x2 << (bit * 2 + 0);
		half &= ~(1 << bit);
	}
	STM32_GPIO_AFRL(port) = afr;

	/* High half of the GPIO bank */
	half = mask >> 8;
	afr = STM32_GPIO_AFRH(port);
	while (half) {
		bit = 31 - __builtin_clz(half);
		afr &= ~(0xf << (bit * 4));
		afr |= func << (bit * 4);
		moder &= ~(0x3 << (bit * 2 + 16));
		moder |= 0x2 << (bit * 2 + 16);
		half &= ~(1 << bit);
	}
	STM32_GPIO_AFRH(port) = afr;
	STM32_GPIO_MODER(port) = moder;
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return !!(STM32_GPIO_IDR(gpio_list[signal].port) &
		  gpio_list[signal].mask);
}

uint16_t *gpio_get_level_reg(enum gpio_signal signal, uint32_t *mask)
{
	*mask = gpio_list[signal].mask;
	return (uint16_t *)&STM32_GPIO_IDR(gpio_list[signal].port);
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	STM32_GPIO_BSRR(gpio_list[signal].port) =
			gpio_list[signal].mask << (value ? 0 : 16);
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	uint32_t bit, group, shift, bank;

	/* Fail if not implemented or no interrupt handler */
	if (!g->mask || !g->irq_handler)
		return EC_ERROR_INVAL;

	bit = 31 - __builtin_clz(g->mask);

	if (exti_events[bit]) {
		CPRINTF("Overriding %s with %s on EXTI%d\n",
			exti_events[bit]->name, g->name, bit);
	}
	exti_events[bit] = g;

	group = bit / 4;
	shift = (bit % 4) * 4;
	bank = (g->port - STM32_GPIOA_BASE) / 0x400;
	STM32_SYSCFG_EXTICR(group) = (STM32_SYSCFG_EXTICR(group) &
			~(0xF << shift)) | (bank << shift);
	STM32_EXTI_IMR |= g->mask;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Interrupt handler */

static void gpio_interrupt(void)
{
	int bit;
	const struct gpio_info *g;
	uint32_t pending = STM32_EXTI_PR;

	STM32_EXTI_PR = pending;

	while (pending) {
		bit = 31 - __builtin_clz(pending);
		g = exti_events[bit];
		if (g && g->irq_handler)
			g->irq_handler(g - gpio_list);
		pending &= ~(1 << bit);
	}
}
DECLARE_IRQ(STM32_IRQ_EXTI0, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI1, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI2, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI3, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI4, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI9_5, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI15_10, gpio_interrupt, 1);
