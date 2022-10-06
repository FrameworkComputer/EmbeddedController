/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 GPIO module for Chrome EC */

#include "clock.h"
#include "console.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "switch.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "registers.h"
#include "gpio_regs.h"

#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ##args)
#define CPRINTS(format, args...) cprints(CC_GPIO, format, ##args)

/* 0-terminated list of GPIO base addresses */
static mxc_gpio_regs_t *gpio_bases[] = { MXC_GPIO0, 0 };

void gpio_set_alternate_function(uint32_t port, uint32_t mask,
				 enum gpio_alternate_func func)
{
	mxc_gpio_regs_t *gpio = MXC_GPIO_GET_GPIO(port);

	switch (func) {
	case GPIO_ALT_FUNC_1:
		gpio->en_clr = mask;
		gpio->en1_clr = mask;
		break;
	case GPIO_ALT_FUNC_2:
		gpio->en_clr = mask;
		gpio->en1_set = mask;
		break;
	case GPIO_ALT_FUNC_3:
		gpio->en_set = mask;
		gpio->en1_set = mask;
		break;
	default:
		/* Default as input */
		gpio->out_en_clr = mask;
		gpio->en_set = mask;
		gpio->en1_clr = mask;
		break;
	}
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	mxc_gpio_regs_t *gpio = MXC_GPIO_GET_GPIO(gpio_list[signal].port);

	return (gpio->in & gpio_list[signal].mask);
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	mxc_gpio_regs_t *gpio = MXC_GPIO_GET_GPIO(gpio_list[signal].port);

	if (value) {
		gpio->out_set = gpio_list[signal].mask;
	} else {
		gpio->out_clr = gpio_list[signal].mask;
	}
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	mxc_gpio_regs_t *gpio = MXC_GPIO_GET_GPIO(port);

	/* Setup for either an output or input. */
	if (flags & GPIO_OUTPUT) {
		gpio->out_en_set = mask;
		gpio->en_set = mask;
		gpio->en1_clr = mask;
	} else {
		gpio->out_en_clr = mask;
		gpio->en_set = mask;
		gpio->en1_clr = mask;
	}

	/* Handle pull up, pull down or neither. */
	if (flags & GPIO_PULL_UP) {
		gpio->pad_cfg1 |= mask;
		gpio->pad_cfg2 &= ~mask;
		gpio->ps |= mask;
	} else if (flags & GPIO_PULL_DOWN) {
		gpio->pad_cfg1 &= ~mask;
		gpio->pad_cfg2 |= mask;
		gpio->ps &= ~mask;
	} else {
		/* No pull up/down */
		gpio->pad_cfg1 &= ~mask;
		gpio->pad_cfg2 &= ~mask;
		gpio->ps &= ~mask;
	}

	/* Handle setting up level interrupts. */
	if (flags & GPIO_INT_F_HIGH) {
		/* Level triggered mode. */
		gpio->int_mod &= ~mask;
		/* Level high generates interrupt. */
		gpio->int_pol |= mask;
	} else if (flags & GPIO_INT_F_LOW) {
		/* Level triggered mode. */
		gpio->int_mod &= ~mask;
		/* Level low generates interrupt. */
		gpio->int_pol &= ~mask;
	}

	/* Handle setting up edge interrupts. */
	if (flags & GPIO_INT_F_RISING) {
		/* Edge triggered mode. */
		gpio->int_mod |= mask;
		/* Rising edge generates interrupt. */
		gpio->int_pol |= mask;
	} else if (flags & GPIO_INT_F_FALLING) {
		/* Edge triggered mode. */
		gpio->int_mod |= mask;
		/* Falling edge generates interrupt. */
		gpio->int_pol &= ~mask;
	}

	/* Handle interrupting on both edges. */
	if ((flags & GPIO_INT_F_RISING) && (flags & GPIO_INT_F_FALLING)) {
		/* Dual edge triggered mode. */
		gpio->int_dual_edge |= mask;
	} else {
		/* Not dual edge triggered mode. */
		gpio->int_dual_edge &= ~mask;
	}

	/* Set the gpio pin high or low. */
	if (flags & GPIO_HIGH) {
		gpio->out_set = mask;
	} else if (flags & GPIO_LOW) {
		gpio->out_clr = mask;
	}
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	mxc_gpio_regs_t *gpio = MXC_GPIO_GET_GPIO(gpio_list[signal].port);

	gpio->int_en_set = gpio_list[signal].mask;
	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	mxc_gpio_regs_t *gpio = MXC_GPIO_GET_GPIO(gpio_list[signal].port);

	gpio->int_en_clr = gpio_list[signal].mask;
	return EC_SUCCESS;
}

int gpio_clear_pending_interrupt(enum gpio_signal signal)
{
	mxc_gpio_regs_t *gpio = MXC_GPIO_GET_GPIO(gpio_list[signal].port);

	gpio->int_clr = gpio_list[signal].mask;
	return EC_SUCCESS;
}

void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int i;

	/* Mask all GPIO interrupts */
	for (i = 0; gpio_bases[i]; i++)
		gpio_bases[i]->int_en = 0;

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		int flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		/* Use as GPIO, not alternate function */
		gpio_set_alternate_function(g->port, g->mask,
					    GPIO_ALT_FUNC_NONE);

		/* Set up GPIO based on flags */
		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}
}

static void gpio_init(void)
{
	/*
	 * Enable global GPIO0 Port interrupt. Note that interrupts still need
	 * to be enabled at the per pin level.
	 */
	task_enable_irq(EC_GPIO0_IRQn);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Interrupt handlers */

/**
 * Handle a GPIO interrupt.
 *
 * port		GPIO port
 * mis		Masked interrupt status value for that port
 */
static void gpio_interrupt(int port, uint32_t mis)
{
	int i = 0;
	const struct gpio_info *g = gpio_list;

	for (i = 0; i < GPIO_IH_COUNT && mis; i++, g++) {
		if (port == g->port && (mis & g->mask)) {
			gpio_irq_handlers[i](i);
			mis &= ~g->mask;
		}
	}
}

/**
 * Handlers for each GPIO port. Read the interrupt status, call the common GPIO
 * interrupt handler and clear the GPIO hardware interrupt status.
 */
#define GPIO_IRQ_FUNC(irqfunc, gpiobase)                             \
	static void irqfunc(void)                                    \
	{                                                            \
		mxc_gpio_regs_t *gpio = MXC_GPIO_GET_GPIO(gpiobase); \
		uint32_t mis = gpio->int_stat;                       \
		gpio_interrupt(gpiobase, mis);                       \
		gpio->int_clr = mis;                                 \
	}

GPIO_IRQ_FUNC(__gpio_0_interrupt, PORT_0);
#undef GPIO_IRQ_FUNC
DECLARE_IRQ(EC_GPIO0_IRQn, __gpio_0_interrupt, 1);
