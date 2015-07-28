/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USART driver for Chrome EC */

#include "atomic.h"
#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "usart.h"
#include "util.h"

void usart_init(struct usart_config const *config)
{
	intptr_t base = config->hw->base;

	/*
	 * Enable clock to USART, this must be done first, before attempting
	 * to configure the USART.
	 */
	*(config->hw->clock_register) |= config->hw->clock_enable;

	/*
	 * For STM32F3, A delay of 1 APB clock cycles is needed before we
	 * can access any USART register. Fortunately, we have
	 * gpio_config_module() below and thus don't need to add the delay.
	 */

	/*
	 * Switch all GPIOs assigned to the USART module over to their USART
	 * alternate functions.
	 */
	gpio_config_module(MODULE_USART, 1);

	/*
	 * 8N1, 16 samples per bit. error interrupts, and special modes
	 * disabled.
	 */
	STM32_USART_CR1(base) = 0x0000;
	STM32_USART_CR2(base) = 0x0000;
	STM32_USART_CR3(base) = 0x0000;

	/*
	 * Enable the RX, TX, and variant specific HW.
	 */
	config->rx->init(config);
	config->tx->init(config);
	config->hw->ops->enable(config);

	/*
	 * Clear error counts.
	 */
	config->state->rx_overrun = 0;
	config->state->rx_dropped = 0;

	/*
	 * Enable the USART, this must be done last since most of the
	 * configuration bits require that the USART be disabled for writes to
	 * succeed.
	 */
	STM32_USART_CR1(base) |= STM32_USART_CR1_UE;
}

void usart_shutdown(struct usart_config const *config)
{
	STM32_USART_CR1(config->hw->base) &= ~STM32_USART_CR1_UE;

	config->hw->ops->disable(config);
}

void usart_set_baud_f0_l(struct usart_config const *config, int frequency_hz)
{
	int      div  = DIV_ROUND_NEAREST(frequency_hz, config->baud);
	intptr_t base = config->hw->base;

	if (div / 16 > 0) {
		/*
		 * CPU clock is high enough to support x16 oversampling.
		 * BRR = (div mantissa)<<4 | (4-bit div fraction)
		 */
		STM32_USART_CR1(base) &= ~STM32_USART_CR1_OVER8;
		STM32_USART_BRR(base) = div;
	} else {
		/*
		 * CPU clock is low; use x8 oversampling.
		 * BRR = (div mantissa)<<4 | (3-bit div fraction)
		 */
		STM32_USART_BRR(base) = ((div / 8) << 4) | (div & 7);
		STM32_USART_CR1(base) |= STM32_USART_CR1_OVER8;
	}
}

void usart_set_baud_f(struct usart_config const *config, int frequency_hz)
{
	int div = DIV_ROUND_NEAREST(frequency_hz, config->baud);

	/* STM32F only supports x16 oversampling */
	STM32_USART_BRR(config->hw->base) = div;
}

void usart_interrupt(struct usart_config const *config)
{
	config->tx->interrupt(config);
	config->rx->interrupt(config);
}
