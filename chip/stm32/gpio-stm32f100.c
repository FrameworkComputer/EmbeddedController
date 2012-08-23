/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "board.h"
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

/* helper function for generating bitmasks for STM32 GPIO config registers */
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

int gpio_set_flags(enum gpio_signal signal, int flags)
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
		/* FIXME: This assumes output max speed of 10MHz */
		mask |= 0x11111111 & mode;
		if (flags & GPIO_OPEN_DRAIN)
			mask |= 0x44444444 & cnf;
	} else {
		/* GPIOx_ODR determines which resistor to activate in
		 * input mode, see Table 16 (datasheet rm0041) */
		if ((flags & GPIO_PULL_UP) == GPIO_PULL_UP) {
			mask |= 0x88888888 & cnf;
			STM32_GPIO_BSRR_OFF(g->port) |= g->mask;
			gpio_set_level(signal, 1);
		} else if ((flags & GPIO_PULL_DOWN) == GPIO_PULL_DOWN) {
			mask |= 0x88888888 & cnf;
			gpio_set_level(signal, 0);
		} else {
			mask |= 0x44444444 & cnf;
		}
	}

	/*
	 * Set pin level after port has been set up as to avoid
	 * potential damage, e.g. driving an open-drain output
	 * high before it has been configured as such.
	 */
	if ((flags & GPIO_OUTPUT) && !is_warm_boot)
		/* General purpose, MODE = 01
		 *
		 * If this is a cold boot, set the level.
		 * On a warm reboot, leave things where they were
		 * or we'll shut off the AP. */
		gpio_set_level(signal, flags & GPIO_HIGH);

	REG32(addr) = mask;

	/* Set up interrupts if necessary */
	ASSERT(!(flags & GPIO_INT_LEVEL));
	if (flags & (GPIO_INT_RISING | GPIO_INT_BOTH))
		STM32_EXTI_RTSR |= g->mask;
	if (flags & (GPIO_INT_FALLING | GPIO_INT_BOTH))
		STM32_EXTI_FTSR |= g->mask;
	/* Interrupt is enabled by gpio_enable_interrupt() */

	return EC_SUCCESS;
}


int gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int i;

	if (STM32_RCC_APB1ENR & 1) {
		/* This is a warm reboot : TIM2 is already active */
		is_warm_boot = 1;
	} else {
		/* Enable all GPIOs clocks
		 * TODO: more fine-grained enabling for power saving
		 */
		STM32_RCC_APB2ENR |= 0x1fd;
	}

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++)
		gpio_set_flags(i, g->flags);

	return EC_SUCCESS;
}


int gpio_init(void)
{
	/* Enable IRQs now that pins are set up */
	task_enable_irq(STM32_IRQ_EXTI0);
	task_enable_irq(STM32_IRQ_EXTI1);
	task_enable_irq(STM32_IRQ_EXTI2);
	task_enable_irq(STM32_IRQ_EXTI3);
	task_enable_irq(STM32_IRQ_EXTI4);
	task_enable_irq(STM32_IRQ_EXTI9_5);
	task_enable_irq(STM32_IRQ_EXTI15_10);

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);


void gpio_set_alternate_function(int port, int mask, int func)
{
	int i;
	const struct gpio_info *g = gpio_list;
	uint32_t addr, cnf, mode, val = 0;

	/*
	 * TODO(dhendrix): STM32 GPIO registers do not have free-form
	 * alternate function setup like the STM32, where each pin can
	 * be configured for any alternate function (though not necessarily
	 * in a valid fashion). Instead, pre-determined sets of pins for a
	 * a given alternate function are chosen via a remapping register.
	 *
	 * Consequently, this function becomes very simple and can (should?)
	 * be merged into gpio_pre_init.
	 */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if ((g->port != port) || (g->mask != mask))
			continue;

		gpio_config_info(g, &addr, &mode, &cnf);
		val = REG32(addr) & ~cnf;

		/* switch from general output to alternate output mode */
		if (g->flags & GPIO_OUTPUT) {
			if (g->flags & GPIO_OPEN_DRAIN)
				val |= 0xcccccccc & cnf;
			else
				val |= 0x88888888 & cnf;
		}

		REG32(addr) = val ;
		break;
	}
}


uint16_t *gpio_get_level_reg(enum gpio_signal signal, uint32_t *mask)
{
	*mask = gpio_list[signal].mask;
	return (uint16_t *)&STM32_GPIO_IDR_OFF(gpio_list[signal].port);
}


int gpio_get_level(enum gpio_signal signal)
{
	return !!(STM32_GPIO_IDR_OFF(gpio_list[signal].port) &
		  gpio_list[signal].mask);
}


int gpio_set_level(enum gpio_signal signal, int value)
{
	STM32_GPIO_BSRR_OFF(gpio_list[signal].port) =
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
		CPRINTF("Overriding %s with %s on EXTI%d\n",
			 exti_events[bit]->name, g->name, bit);
	}
#endif
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
