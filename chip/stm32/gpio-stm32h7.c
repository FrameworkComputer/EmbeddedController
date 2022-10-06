/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "gpio_chip.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"

void gpio_enable_clocks(void)
{
	/* Enable all GPIOs clocks */
	STM32_RCC_AHB4ENR |= STM32_RCC_AHB4ENR_GPIOMASK;

	/* Delay 1 AHB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_AHB, 1);
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

static void _gpio_interrupt(void)
{
	gpio_interrupt();
}

DECLARE_IRQ(STM32_IRQ_EXTI0, _gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI1, _gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI2, _gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI3, _gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI4, _gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI9_5, _gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI15_10, _gpio_interrupt, 1);

#include "gpio-f0-l.c"
