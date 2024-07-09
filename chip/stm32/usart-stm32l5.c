/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "compile_time_macros.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "usart-stm32l.h"
#include "util.h"

/*
 * This configs array stores the currently active usart_config structure for
 * each USART, an entry will be NULL if no USART driver is initialized for the
 * corresponding hardware instance.
 */
#define STM32_USARTS_MAX 6

#if defined(CONFIG_USART_SYSCLK)
#define USART_CLOCK_FREQ (clock_get_freq())
#elif defined(CONFIG_USART_HSI16)
#define USART_CLOCK_FREQ (16000000)
#else
#define USART_CLOCK_FREQ (clock_get_apb_freq())
#endif

static struct usart_config const *configs[STM32_USARTS_MAX];

struct usart_configs usart_get_configs(void)
{
	return (struct usart_configs){ configs, ARRAY_SIZE(configs) };
}

static void usart_variant_enable(struct usart_config const *config)
{
	/* Use single-bit sampling */
	STM32_USART_CR3(config->hw->base) |= STM32_USART_CR3_ONEBIT;

#if defined(CONFIG_USART_SYSCLK)
	/* Set clock source of the particular UART to core clock */
	STM32_RCC_CCIPR1 &= ~(STM32_RCC_CCIPR_MASK << (2 * config->hw->index));
	STM32_RCC_CCIPR1 |=
		(STM32_RCC_CCIPR_UART_SYSCLK << (2 * config->hw->index));
#elif defined(CONFIG_USART_HSI16)
	/* Set clock source of the particular UART to 16MHz HSI */
	STM32_RCC_CCIPR1 &= ~(STM32_RCC_CCIPR_MASK << (2 * config->hw->index));
	STM32_RCC_CCIPR1 |=
		(STM32_RCC_CCIPR_UART_HSI16 << (2 * config->hw->index));
#else
	/* Leave at power-on default, which is APB PCLK */
#endif

	/*
	 * Make sure we register this config before enabling the HW.
	 * If we did it the other way around the FREQ_CHANGE hook could be
	 * called before we update the configs array and we would miss the
	 * clock frequency change event, leaving our baud rate divisor wrong.
	 */
	configs[config->hw->index] = config;

	usart_set_baud_f0_l(config, config->baud, USART_CLOCK_FREQ);

	task_enable_irq(config->hw->irq);
}

#ifdef CONFIG_STREAM_USB
int usart_get_baud(struct usart_config const *config)
{
	return usart_get_baud_f0_l(config, USART_CLOCK_FREQ);
}
#endif

void usart_set_baud(struct usart_config const *config, int baud)
{
	usart_set_baud_f0_l(config, baud, USART_CLOCK_FREQ);
}

static void usart_variant_disable(struct usart_config const *config)
{
	task_disable_irq(config->hw->irq);

	configs[config->hw->index] = NULL;
}

static struct usart_hw_ops const usart_variant_hw_ops = {
	.enable = usart_variant_enable,
	.disable = usart_variant_disable,
};

#ifndef CONFIG_USART_HSI16
static void freq_change(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(configs); ++i)
		if (configs[i])
			usart_set_baud_f0_l(configs[i], configs[i]->baud,
					    USART_CLOCK_FREQ);
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, freq_change, HOOK_PRIO_DEFAULT);
#endif

void usart_clear_tc(struct usart_config const *config)
{
	STM32_USART_SR(config->hw->base) &= ~STM32_USART_SR_TC;
}

/*
 * USART interrupt bindings.  These functions can not be defined as static or
 * they will be removed by the linker because of the way that DECLARE_IRQ works.
 */
#if defined(CONFIG_STREAM_USART1)
struct usart_hw_config const usart1_hw = {
	.index = 0, /* Must match position of this UART in RCC_CCIPR1 */
	.base = STM32_USART1_BASE,
	.irq = STM32_IRQ_USART1,
	.clock_register = &STM32_RCC_APB2ENR,
	.clock_enable = STM32_RCC_PB2_USART1,
	.ops = &usart_variant_hw_ops,
};

static void usart1_interrupt(void)
{
	usart_interrupt(configs[0]);
}

DECLARE_IRQ(STM32_IRQ_USART1, usart1_interrupt, 2);
#endif

#if defined(CONFIG_STREAM_USART2)
struct usart_hw_config const usart2_hw = {
	.index = 1, /* Must match position of this UART in RCC_CCIPR1 */
	.base = STM32_USART2_BASE,
	.irq = STM32_IRQ_USART2,
	.clock_register = &STM32_RCC_APB1ENR,
	.clock_enable = STM32_RCC_PB1_USART2,
	.ops = &usart_variant_hw_ops,
};

static void usart2_interrupt(void)
{
	usart_interrupt(configs[1]);
}

DECLARE_IRQ(STM32_IRQ_USART2, usart2_interrupt, 2);
#endif

#if defined(CONFIG_STREAM_USART3)
struct usart_hw_config const usart3_hw = {
	.index = 2, /* Must match position of this UART in RCC_CCIPR1 */
	.base = STM32_USART3_BASE,
	.irq = STM32_IRQ_USART3,
	.clock_register = &STM32_RCC_APB1ENR,
	.clock_enable = STM32_RCC_PB1_USART3,
	.ops = &usart_variant_hw_ops,
};

static void usart3_interrupt(void)
{
	usart_interrupt(configs[2]);
}

DECLARE_IRQ(STM32_IRQ_USART3, usart3_interrupt, 2);
#endif

#if defined(CONFIG_STREAM_USART4)
struct usart_hw_config const usart4_hw = {
	.index = 3, /* Must match position of this UART in RCC_CCIPR1 */
	.base = STM32_USART4_BASE,
	.irq = STM32_IRQ_USART4,
	.clock_register = &STM32_RCC_APB1ENR,
	.clock_enable = STM32_RCC_PB1_USART4,
	.ops = &usart_variant_hw_ops,
};

static void usart4_interrupt(void)
{
	usart_interrupt(configs[3]);
}

DECLARE_IRQ(STM32_IRQ_USART4, usart4_interrupt, 2);
#endif

#if defined(CONFIG_STREAM_USART5)
struct usart_hw_config const usart5_hw = {
	.index = 4, /* Must match position of this UART in RCC_CCIPR1 */
	.base = STM32_USART5_BASE,
	.irq = STM32_IRQ_USART5,
	.clock_register = &STM32_RCC_APB1ENR,
	.clock_enable = STM32_RCC_PB1_USART5,
	.ops = &usart_variant_hw_ops,
};

static void usart5_interrupt(void)
{
	usart_interrupt(configs[4]);
}

DECLARE_IRQ(STM32_IRQ_USART5, usart5_interrupt, 2);
#endif

#if defined(CONFIG_STREAM_USART9)
struct usart_hw_config const usart9_hw = {
	.index = 5, /* Must match position of this UART in RCC_CCIPR1 */
	.base = STM32_USART9_BASE,
	.irq = STM32_IRQ_USART9,
	.clock_register = &STM32_RCC_APB1ENR2,
	.clock_enable = STM32_RCC_APB1ENR2_LPUART1EN,
	.ops = &usart_variant_hw_ops,
};

static void usart9_interrupt(void)
{
	usart_interrupt(configs[5]);
}

DECLARE_IRQ(STM32_IRQ_USART9, usart9_interrupt, 2);
#endif
