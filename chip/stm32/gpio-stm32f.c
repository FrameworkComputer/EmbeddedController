/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
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

/*
 * Special precautions must be taken in order to avoid accidentally rebooting
 * the AP if we are warm rebooting the EC such as during sysjump.
 */
static int is_warm_boot;

/* For each EXTI bit, record which GPIO entry is using it */
static const struct gpio_info *exti_events[16];

struct port_config {
	uint32_t addr;
	uint32_t mode;
	uint32_t cnf;
};

/**
 * Helper function for generating bitmasks for STM32 GPIO config registers
 */
static void gpio_config_info(const struct gpio_info *g, uint32_t *addr,
		uint32_t *mode, uint32_t *cnf) {
	/*
	 * 2-bit config followed by 2-bit mode for each pin, each
	 * successive pin raises the exponent for the lowest bit
	 * set by an order of 4, e.g. 2^0, 2^4, 2^8, etc.
	 */
	if (g->mask & 0xff) {
		*addr = g->port;	/* GPIOx_CRL */
		*mode = g->mask;
	} else {
		*addr = g->port + 0x04;	/* GPIOx_CRH */
		*mode = g->mask >> 8;
	}
	*mode = *mode * *mode * *mode * *mode;
	*mode |= *mode << 1;
	*cnf = *mode << 2;
}

void gpio_set_flags(enum gpio_signal signal, int flags)
{
	const struct gpio_info *g = gpio_list + signal;
	uint32_t addr, cnf, mode, mask;

	gpio_config_info(g, &addr, &mode, &cnf);
	mask = REG32(addr) & ~(cnf | mode);

	/*
	 * For STM32, the port configuration field changes meaning
	 * depending on whether the port is an input, analog input,
	 * output, or alternate function.
	 */
	if (flags & GPIO_OUTPUT) {
		/* TODO: This assumes output max speed of 10MHz */
		mask |= 0x11111111 & mode;
		if (flags & GPIO_OPEN_DRAIN)
			mask |= 0x44444444 & cnf;

	} else {
		/*
		 * GPIOx_ODR determines which resistor to activate in
		 * input mode, see Table 16 (datasheet rm0041)
		 */
		if (flags & GPIO_PULL_UP) {
			mask |= 0x88888888 & cnf;
			gpio_set_level(signal, 1);
		} else if (flags & GPIO_PULL_DOWN) {
			mask |= 0x88888888 & cnf;
			gpio_set_level(signal, 0);
		} else {
			mask |= 0x44444444 & cnf;
		}
	}

	REG32(addr) = mask;

	if (flags & GPIO_OUTPUT) {
		/*
		 * Set pin level after port has been set up as to avoid
		 * potential damage, e.g. driving an open-drain output high
		 * before it has been configured as such.
		 */
		if (flags & GPIO_HIGH)
			gpio_set_level(signal, 1);
		else if (flags & GPIO_LOW)
			gpio_set_level(signal, 0);
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
	int i;

	if (STM32_RCC_APB1ENR & 1) {
		/* This is a warm reboot : TIM2 is already active */
		is_warm_boot = 1;
	} else {
		/*
		 * Enable all GPIOs clocks
		 *
		 * TODO: more fine-grained enabling for power saving
		 */
		STM32_RCC_APB2ENR |= 0x1fd;
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
		if (is_warm_boot)
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		/* Set up GPIO based on flags */
		gpio_set_flags(i, flags);
	}
}

void gpio_init(void)
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

uint16_t *gpio_get_level_reg(enum gpio_signal signal, uint32_t *mask)
{
	*mask = gpio_list[signal].mask;
	return (uint16_t *)&STM32_GPIO_IDR(gpio_list[signal].port);
}


test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return !!(STM32_GPIO_IDR(gpio_list[signal].port) &
		  gpio_list[signal].mask);
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
	STM32_AFIO_EXTICR(group) = (STM32_AFIO_EXTICR(group) &
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
