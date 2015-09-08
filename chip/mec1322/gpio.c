/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for MEC1322 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

struct gpio_int_mapping {
	int8_t girq_id;
	int8_t port_offset;
};

/* Mapping from GPIO port to GIRQ info */
static const struct gpio_int_mapping int_map[22] = {
	{11, 0}, {11, 0}, {11, 0}, {11, 0},
	{10, 4}, {10, 4}, {10, 4}, {-1, -1},
	{-1, -1}, {-1, -1}, {9, 10}, {9, 10},
	{9, 10}, {9, 10}, {8, 14}, {8, 14},
	{8, 14}, {-1, -1}, {-1, -1}, {-1, -1},
	{20, 20}, {20, 20}
};

void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func)
{
	int i;
	uint32_t val;

	while (mask) {
		i = __builtin_ffs(mask) - 1;
		val = MEC1322_GPIO_CTL(port, i);
		val &= ~((1 << 12) | (1 << 13));
		/* mux_control = 0 indicates GPIO */
		if (func > 0)
			val |= (func & 0x3) << 12;
		MEC1322_GPIO_CTL(port, i) = val;
		mask &= ~(1 << i);
	}
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	uint32_t mask = gpio_list[signal].mask;
	int i;
	uint32_t val;

	if (mask == 0)
		return 0;
	i = GPIO_MASK_TO_NUM(mask);
	val = MEC1322_GPIO_CTL(gpio_list[signal].port, i);

	return (val & (1 << 24)) ? 1 : 0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	uint32_t mask = gpio_list[signal].mask;
	int i;

	if (mask == 0)
		return;
	i = GPIO_MASK_TO_NUM(mask);

	if (value)
		MEC1322_GPIO_CTL(gpio_list[signal].port, i) |= (1 << 16);
	else
		MEC1322_GPIO_CTL(gpio_list[signal].port, i) &= ~(1 << 16);
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	int i;
	uint32_t val;
	while (mask) {
		i = GPIO_MASK_TO_NUM(mask);
		mask &= ~(1 << i);
		val = MEC1322_GPIO_CTL(port, i);

		/*
		 * Select open drain first, so that we don't glitch the signal
		 * when changing the line to an output.
		 */
		if (flags & GPIO_OPEN_DRAIN)
			val |= (1 << 8);
		else
			val &= ~(1 << 8);

		if (flags & GPIO_OUTPUT) {
			val |= (1 << 9);
			val &= ~(1 << 10);
		} else {
			val &= ~(1 << 9);
			val |= (1 << 10);
		}

		/* Handle pullup / pulldown */
		if (flags & GPIO_PULL_UP)
			val = (val & ~0x3) | 0x1;
		else if (flags & GPIO_PULL_DOWN)
			val = (val & ~0x3) | 0x2;
		else
			val &= ~0x3;

		/* Set up interrupt */
		if (flags & (GPIO_INT_F_RISING | GPIO_INT_F_FALLING))
			val |= (1 << 7);
		else
			val &= ~(1 << 7);

		val &= ~(0x7 << 4);

		if ((flags & GPIO_INT_F_RISING) && (flags & GPIO_INT_F_FALLING))
			val |= 0x7 << 4;
		else if (flags & GPIO_INT_F_RISING)
			val |= 0x5 << 4;
		else if (flags & GPIO_INT_F_FALLING)
			val |= 0x6 << 4;
		else if (flags & GPIO_INT_F_HIGH)
			val |= 0x1 << 4;
		else if (!(flags & GPIO_INT_F_LOW)) /* No interrupt flag set */
			val |= 0x4 << 4;

		/* Use as GPIO */
		val &= ~((1 << 12) | (1 << 13));

		/* Set up level */
		if (flags & GPIO_HIGH)
			val |= (1 << 16);
		else if (flags & GPIO_LOW)
			val &= ~(1 << 16);

		MEC1322_GPIO_CTL(port, i) = val;
	}
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	int i, port, girq_id, bit_id;

	if (gpio_list[signal].mask == 0)
		return EC_SUCCESS;

	i = GPIO_MASK_TO_NUM(gpio_list[signal].mask);
	port = gpio_list[signal].port;
	girq_id = int_map[port].girq_id;
	bit_id = (port - int_map[port].port_offset) * 8 + i;

	MEC1322_INT_ENABLE(girq_id) |= (1 << bit_id);
	MEC1322_INT_BLK_EN |= (1 << girq_id);

	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	int i, port, girq_id, bit_id;

	if (gpio_list[signal].mask == 0)
		return EC_SUCCESS;

	i = GPIO_MASK_TO_NUM(gpio_list[signal].mask);
	port = gpio_list[signal].port;
	girq_id = int_map[port].girq_id;
	bit_id = (port - int_map[port].port_offset) * 8 + i;

	MEC1322_INT_DISABLE(girq_id) |= (1 << bit_id);

	return EC_SUCCESS;
}

void gpio_pre_init(void)
{
	int i;
	int flags;
	int is_warm = gpio_is_reboot_warm();
	const struct gpio_info *g = gpio_list;


	for (i = 0; i < GPIO_COUNT; i++, g++) {
		flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		/*
		 * If this is a warm reboot, don't set the output levels or
		 * we'll shut off the AP.
		 */
		if (is_warm)
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}
}

/* Clear any interrupt flags before enabling GPIO interrupt */
#define ENABLE_GPIO_GIRQ(x) \
	do { \
		MEC1322_INT_SOURCE(x) |= MEC1322_INT_RESULT(x); \
		task_enable_irq(MEC1322_IRQ_GIRQ ## x); \
	} while (0)

static void gpio_init(void)
{
	ENABLE_GPIO_GIRQ(8);
	ENABLE_GPIO_GIRQ(9);
	ENABLE_GPIO_GIRQ(10);
	ENABLE_GPIO_GIRQ(11);
	ENABLE_GPIO_GIRQ(20);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Interrupt handlers */


/**
 * Handler for each GIRQ interrupt. This reads and clears the interrupt bits for
 * the GIRQ interrupt, then finds and calls the corresponding GPIO interrupt
 * handlers.
 *
 * @param girq		GIRQ index
 * @param port_offset	GPIO port offset for the given GIRQ
 */
static void gpio_interrupt(int girq, int port_offset)
{
	int i, bit;
	const struct gpio_info *g = gpio_list;
	uint32_t sts = MEC1322_INT_RESULT(girq);

	MEC1322_INT_SOURCE(girq) |= sts;

	for (i = 0; i < GPIO_IH_COUNT && sts; ++i, ++g) {
		bit = (g->port - port_offset) * 8 + __builtin_ffs(g->mask) - 1;
		if (sts & (1 << bit))
			gpio_irq_handlers[i](i);
		sts &= ~(1 << bit);
	}
}

#define GPIO_IRQ_FUNC(irqfunc, girq, port_offset)  \
	void irqfunc(void)                         \
	{                                          \
		gpio_interrupt(girq, port_offset); \
	}

GPIO_IRQ_FUNC(__girq_8_interrupt, 8, 14);
GPIO_IRQ_FUNC(__girq_9_interrupt, 9, 10);
GPIO_IRQ_FUNC(__girq_10_interrupt, 10, 4);
GPIO_IRQ_FUNC(__girq_11_interrupt, 11, 0);
GPIO_IRQ_FUNC(__girq_20_interrupt, 20, 20);

#undef GPIO_IRQ_FUNC

/*
 * Declare IRQs.  Nesting this macro inside the GPIO_IRQ_FUNC macro works
 * poorly because DECLARE_IRQ() stringizes its inputs.
 */
DECLARE_IRQ(MEC1322_IRQ_GIRQ8, __girq_8_interrupt, 1);
DECLARE_IRQ(MEC1322_IRQ_GIRQ9, __girq_9_interrupt, 1);
DECLARE_IRQ(MEC1322_IRQ_GIRQ10, __girq_10_interrupt, 1);
DECLARE_IRQ(MEC1322_IRQ_GIRQ11, __girq_11_interrupt, 1);
DECLARE_IRQ(MEC1322_IRQ_GIRQ20, __girq_20_interrupt, 1);
