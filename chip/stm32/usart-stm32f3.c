/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usart-stm32f3.h"

#include "common.h"
#include "compile_time_macros.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/*
 * This configs array stores the currently active usart_config structure for
 * each USART, an entry will be NULL if no USART driver is initialized for the
 * corresponding hardware instance.
 */
#define STM32_USARTS_MAX 3

static struct usart_config const *configs[STM32_USARTS_MAX];

struct usart_configs usart_get_configs(void)
{
	return (struct usart_configs) {configs, ARRAY_SIZE(configs)};
}

static void usart_variant_enable(struct usart_config const *config)
{
	configs[config->hw->index] = config;

	/*
	 * All three USARTS are clocked from the HSI(8MHz) source.  This is
	 * done because the clock sources elsewhere are setup so that the result
	 * of clock_get_freq() is not the input clock frequency to the USARTs
	 * baud rate divisors.
	 */
	STM32_RCC_CFGR3 |= 0x000f0003;

	usart_set_baud_f0_l(config, config->baud, 8000000);

	task_enable_irq(config->hw->irq);
}

static void usart_variant_disable(struct usart_config const *config)
{
	task_disable_irq(config->hw->irq);

	configs[config->hw->index] = NULL;
}

static struct usart_hw_ops const usart_variant_hw_ops = {
	.enable  = usart_variant_enable,
	.disable = usart_variant_disable,
};

void usart_clear_tc(struct usart_config const *config)
{
	STM32_USART_ICR(config->hw->base) |= STM32_USART_ICR_TCCF;
}

/*
 * USART interrupt bindings.  These functions can not be defined as static or
 * they will be removed by the linker because of the way that DECLARE_IRQ works.
 */
#if defined(CONFIG_STREAM_USART1)
struct usart_hw_config const usart1_hw = {
	.index          = 0,
	.base           = STM32_USART1_BASE,
	.irq            = STM32_IRQ_USART1,
	.clock_register = &STM32_RCC_APB2ENR,
	.clock_enable   = STM32_RCC_PB2_USART1,
	.ops            = &usart_variant_hw_ops,
};

void usart1_interrupt(void)
{
	usart_interrupt(configs[0]);
}

DECLARE_IRQ(STM32_IRQ_USART1, usart1_interrupt, 2);
#endif

#if defined(CONFIG_STREAM_USART2)
struct usart_hw_config const usart2_hw = {
	.index          = 1,
	.base           = STM32_USART2_BASE,
	.irq            = STM32_IRQ_USART2,
	.clock_register = &STM32_RCC_APB1ENR,
	.clock_enable   = STM32_RCC_PB1_USART2,
	.ops            = &usart_variant_hw_ops,
};

void usart2_interrupt(void)
{
	usart_interrupt(configs[1]);
}

DECLARE_IRQ(STM32_IRQ_USART2, usart2_interrupt, 2);
#endif

#if defined(CONFIG_STREAM_USART3)
struct usart_hw_config const usart3_hw = {
	.index          = 2,
	.base           = STM32_USART3_BASE,
	.irq            = STM32_IRQ_USART3,
	.clock_register = &STM32_RCC_APB1ENR,
	.clock_enable   = STM32_RCC_PB1_USART3,
	.ops            = &usart_variant_hw_ops,
};
#endif

#if defined(CONFIG_STREAM_USART3)
void usart3_interrupt(void)
{
	usart_interrupt(configs[2]);
}

DECLARE_IRQ(STM32_IRQ_USART3, usart3_interrupt, 2);
#endif
