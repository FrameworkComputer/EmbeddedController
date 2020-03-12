/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio_chip.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)
/* For each EXTI bit, record which GPIO entry is using it */
static uint8_t exti_events[16];

void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	const struct unused_pin_info *u = unused_pin_list;
	int is_warm = system_is_reboot_warm();
	int i;

	/* Required to configure external IRQ lines (SYSCFG_EXTICRn) */
#ifdef CHIP_FAMILY_STM32H7
	STM32_RCC_APB4ENR |= STM32_RCC_SYSCFGEN;
#else
	STM32_RCC_APB2ENR |= STM32_RCC_SYSCFGEN;
#endif

	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);

	/* Disable all GPIO EXTINTs (EXTINT0..15) left enabled after sysjump. */
	STM32_EXTI_IMR &= ~0xFFFF;

	if (!is_warm)
		gpio_enable_clocks();

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
		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}

	/* Configure optional unused pins for low power optimization. */
	for (i = 0; i < unused_pin_count; i++, u++) {
		/*
		 * Configure unused pins as ANALOG INPUT to save power.
		 * For more info, please see
		 * "USING STM32F4 MCU POWER MODES WITH BEST DYNAMIC EFFICIENCY"
		 * ("AN4365") section 1.2.6 and section 7.3.12 of the STM32F412
		 * reference manual.
		 */
		if (IS_ENABLED(CHIP_FAMILY_STM32F4))
			gpio_set_flags_by_mask(u->port, u->mask, GPIO_ANALOG);
	}
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
	const struct gpio_info *g_old = gpio_list;

	uint32_t bit, group, shift, bank;

	/* Fail if not implemented or no interrupt handler */
	if (!g->mask || signal >= GPIO_IH_COUNT)
		return EC_ERROR_INVAL;

	bit = GPIO_MASK_TO_NUM(g->mask);

	g_old += exti_events[bit];

	if ((exti_events[bit]) && (exti_events[bit] != signal)) {
		CPRINTS("Overriding %s with %s on EXTI%d",
			g_old->name, g->name, bit);
	}
	exti_events[bit] = signal;

	group = bit / 4;
	shift = (bit % 4) * 4;
	bank = (g->port - STM32_GPIOA_BASE) / 0x400;

	STM32_SYSCFG_EXTICR(group) = (STM32_SYSCFG_EXTICR(group) &
				      ~(0xF << shift)) | (bank << shift);
	STM32_EXTI_IMR |= g->mask;

	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	uint32_t bit;

	/* Fail if not implemented or no interrupt handler */
	if (!g->mask || signal >= GPIO_IH_COUNT)
		return EC_ERROR_INVAL;

	STM32_EXTI_IMR &= ~g->mask;

	bit = GPIO_MASK_TO_NUM(g->mask);

	exti_events[bit] = 0;

	return EC_SUCCESS;
}

int gpio_clear_pending_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	if (!g->mask || signal >= GPIO_IH_COUNT)
		return EC_ERROR_INVAL;

	/* Write 1 to clear interrupt */
	STM32_EXTI_PR = g->mask;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Interrupt handler */

void __keep gpio_interrupt(void)
{
	int bit;
	/* process only GPIO EXTINTs (EXTINT0..15) not other EXTINTs */
	uint32_t pending = STM32_EXTI_PR & 0xFFFF;
	uint8_t signal;

	/* Write 1 to clear interrupt */
	STM32_EXTI_PR = pending;

	while (pending) {
		bit = get_next_bit(&pending);
		signal = exti_events[bit];
		if (signal < GPIO_IH_COUNT)
			gpio_irq_handlers[signal](signal);
	}
}
#ifdef CHIP_FAMILY_STM32F0
DECLARE_IRQ(STM32_IRQ_EXTI0_1, gpio_interrupt, STM32_IRQ_EXT0_1_PRIORITY);
DECLARE_IRQ(STM32_IRQ_EXTI2_3, gpio_interrupt, STM32_IRQ_EXT2_3_PRIORITY);
DECLARE_IRQ(STM32_IRQ_EXTI4_15, gpio_interrupt, STM32_IRQ_EXTI4_15_PRIORITY);
#endif
