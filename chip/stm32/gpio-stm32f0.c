/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"

int gpio_is_reboot_warm(void)
{
	return ((STM32_RCC_AHBENR & 0x7e0000) == 0x7e0000);
}

void gpio_enable_clocks(void)
{
	/*
	 * Enable all GPIOs clocks
	 *
	 * TODO(crosbug.com/p/23770): only enable the banks we need to,
	 * and support disabling some of them in low-power idle.
	 */
	STM32_RCC_AHBENR |= 0x7e0000;

	/* Delay 1 AHB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_AHB, 1);
}

static void gpio_init(void)
{
	/* Enable IRQs now that pins are set up */
	task_enable_irq(STM32_IRQ_EXTI0_1);
	task_enable_irq(STM32_IRQ_EXTI2_3);
	task_enable_irq(STM32_IRQ_EXTI4_15);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

DECLARE_IRQ(STM32_IRQ_EXTI0_1, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI2_3, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI4_15, gpio_interrupt, 1);

#include "gpio-f0-l.c"
