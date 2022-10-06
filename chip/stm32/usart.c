/* Copyright 2014 The ChromiumOS Authors
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
	uint32_t cr2, cr3;

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

	cr2 = 0x0000;
	cr3 = 0x0000;
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) || \
	defined(CHIP_FAMILY_STM32L4)
	if (config->flags & USART_CONFIG_FLAG_RX_INV)
		cr2 |= BIT(16);
	if (config->flags & USART_CONFIG_FLAG_TX_INV)
		cr2 |= BIT(17);
#endif
	if (config->flags & USART_CONFIG_FLAG_HDSEL)
		cr3 |= BIT(3);

	STM32_USART_CR1(base) = 0x0000;
	STM32_USART_CR2(base) = cr2;
	STM32_USART_CR3(base) = cr3;

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

void usart_set_baud_f0_l(struct usart_config const *config, int baud,
			 int frequency_hz)
{
	int div = DIV_ROUND_NEAREST(frequency_hz, baud);
	intptr_t base = config->hw->base;

#ifdef STM32_USART9_BASE
	if (config->hw->base == STM32_USART9_BASE) /* LPUART */
		div *= 256;
#endif

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

void usart_set_baud_f(struct usart_config const *config, int baud,
		      int frequency_hz)
{
	int div = DIV_ROUND_NEAREST(frequency_hz, baud);

#ifdef STM32_USART9_BASE
	if (config->hw->base == STM32_USART9_BASE) /* LPUART */
		div *= 256;
#endif

	/* STM32F only supports x16 oversampling */
	STM32_USART_BRR(config->hw->base) = div;
}

int usart_get_parity(struct usart_config const *config)
{
	intptr_t base = config->hw->base;

	if (!(STM32_USART_CR1(base) & STM32_USART_CR1_PCE))
		return 0;
	if (STM32_USART_CR1(base) & STM32_USART_CR1_PS)
		return 1;
	return 2;
}

/*
 * We only allow 8 bit word. CR1_PCE modifies parity enable,
 * CR1_PS modifies even/odd, CR1_M modifies total word length
 * to make room for parity.
 */
void usart_set_parity(struct usart_config const *config, int parity)
{
	uint32_t ue;
	intptr_t base = config->hw->base;

	if ((parity < 0) || (parity > 2))
		return;

	/* Record active state and disable the UART. */
	ue = STM32_USART_CR1(base) & STM32_USART_CR1_UE;
	STM32_USART_CR1(base) &= ~STM32_USART_CR1_UE;

	if (parity) {
		/* Set parity control enable. */
		STM32_USART_CR1(base) |=
			(STM32_USART_CR1_PCE | STM32_USART_CR1_M);
		/* Set parity select even/odd bit. */
		if (parity == 2)
			STM32_USART_CR1(base) &= ~STM32_USART_CR1_PS;
		else
			STM32_USART_CR1(base) |= STM32_USART_CR1_PS;
	} else {
		STM32_USART_CR1(base) &=
			~(STM32_USART_CR1_PCE | STM32_USART_CR1_PS |
			  STM32_USART_CR1_M);
	}

	/* Restore active state. */
	STM32_USART_CR1(base) |= ue;
}

void usart_interrupt(struct usart_config const *config)
{
	config->tx->interrupt(config);
	config->rx->interrupt(config);
}
