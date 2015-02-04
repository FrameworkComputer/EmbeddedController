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

/**
 * Helper function for generating bitmasks for STM32 GPIO config registers
 */
static void gpio_config_info(uint32_t port, uint32_t mask, uint32_t *addr,
		uint32_t *mode, uint32_t *cnf) {
	/*
	 * 2-bit config followed by 2-bit mode for each pin, each
	 * successive pin raises the exponent for the lowest bit
	 * set by an order of 4, e.g. 2^0, 2^4, 2^8, etc.
	 */
	if (mask & 0xff) {
		*addr = port;	/* GPIOx_CRL */
		*mode = mask;
	} else {
		*addr = port + 0x04;	/* GPIOx_CRH */
		*mode = mask >> 8;
	}
	*mode = *mode * *mode * *mode * *mode;
	*mode |= *mode << 1;
	*cnf = *mode << 2;
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t pmask, uint32_t flags)
{
	uint32_t addr, cnf, mode, mask;

	gpio_config_info(port, pmask, &addr, &mode, &cnf);
	mask = REG32(addr) & ~(cnf | mode);

	/*
	 * For STM32, the port configuration field changes meaning
	 * depending on whether the port is an input, analog input,
	 * output, or alternate function.
	 */
	if (flags & GPIO_OUTPUT) {
		/*
		 * This sets output max speed to 10MHz.  That should be
		 * sufficient for most GPIO needs; the only thing that needs to
		 * go faster is SPI, which overrides the port speed on its own.
		 */
		mask |= 0x11111111 & mode;
		if (flags & GPIO_OPEN_DRAIN)
			mask |= 0x44444444 & cnf;

	} else {
		/*
		 * GPIOx_ODR determines which resistor to activate in
		 * input mode, see Table 16 (datasheet rm0041)
		 */
		if (flags & GPIO_ANALOG) {
			/* Analog input, MODE=00 CNF=00 */
			/* the 4 bits in mask are already reset above */
		} else if (flags & GPIO_PULL_UP) {
			mask |= 0x88888888 & cnf;
			STM32_GPIO_BSRR(port) = pmask;
		} else if (flags & GPIO_PULL_DOWN) {
			mask |= 0x88888888 & cnf;
			STM32_GPIO_BSRR(port) = pmask << 16;
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
			STM32_GPIO_BSRR(port) = pmask;
		else if (flags & GPIO_LOW)
			STM32_GPIO_BSRR(port) = pmask << 16;
	}

	/* Set up interrupts if necessary */
	ASSERT(!(flags & (GPIO_INT_F_LOW | GPIO_INT_F_HIGH)));
	if (flags & GPIO_INT_F_RISING)
		STM32_EXTI_RTSR |= pmask;
	if (flags & GPIO_INT_F_FALLING)
		STM32_EXTI_FTSR |= pmask;
	/* Interrupt is enabled by gpio_enable_interrupt() */
}

int gpio_is_reboot_warm(void)
{
	return (STM32_RCC_APB1ENR & 1);
}

void gpio_enable_clocks(void)
{
	/*
	 * Enable all GPIOs clocks
	 *
	 * TODO(crosbug.com/p/23770): only enable the banks we need to,
	 * and support disabling some of them in low-power idle.
	 */
#ifdef CHIP_VARIANT_STM32TS60
	STM32_RCC_APB2ENR |= 0x7fd;
#else
	STM32_RCC_APB2ENR |= 0x1fd;
#endif

	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);
}

void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func)
{
	/* TODO(crosbug.com/p/21618): implement me! */
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

DECLARE_IRQ(STM32_IRQ_EXTI0, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI1, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI2, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI3, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI4, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI9_5, gpio_interrupt, 1);
DECLARE_IRQ(STM32_IRQ_EXTI15_10, gpio_interrupt, 1);
