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

static void usart_written(struct consumer const *consumer, size_t count)
{
	struct usart_config const *config =
		DOWNCAST(consumer, struct usart_config, consumer);

	/*
	 * Enable USART interrupt.  This causes the USART interrupt handler to
	 * start fetching from the TX queue if it wasn't already.
	 */
	if (count)
		STM32_USART_CR1(config->hw->base) |= STM32_USART_CR1_TXEIE;
}

static void usart_flush(struct consumer const *consumer)
{
	struct usart_config const *config =
		DOWNCAST(consumer, struct usart_config, consumer);

	/*
	 * Enable USART interrupt.  This causes the USART interrupt handler to
	 * start fetching from the TX queue if it wasn't already.
	 */
	STM32_USART_CR1(config->hw->base) |= STM32_USART_CR1_TXEIE;

	while (queue_count(consumer->queue))
		;
}

struct producer_ops const usart_producer_ops = {
	/*
	 * Nothing to do here, we either had enough space in the queue when
	 * a character came in or we dropped it already.
	 */
	.read = NULL,
};

struct consumer_ops const usart_consumer_ops = {
	.written = usart_written,
	.flush   = usart_flush,
};

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
	 * 8N1, 16 samples per bit, enable TX and RX (and associated RX
	 * interrupt) DMA, error interrupts, and special modes disabled.
	 */
	STM32_USART_CR1(base) = (STM32_USART_CR1_TE |
				 STM32_USART_CR1_RE |
				 STM32_USART_CR1_RXNEIE);
	STM32_USART_CR2(base) = 0x0000;
	STM32_USART_CR3(base) = STM32_USART_CR3_OVRDIS;

	/*
	 * Enable the variant specific HW.
	 */
	config->hw->ops->enable(config);

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

static void usart_interrupt_tx(struct usart_config const *config)
{
	intptr_t base = config->hw->base;
	uint8_t  byte;

	if (consumer_read_unit(&config->consumer, &byte)) {
		STM32_USART_TDR(base) = byte;

		/*
		 * Make sure the TXE interrupt is enabled and that we won't go
		 * into deep sleep.  This invocation of the USART interrupt
		 * handler may have been manually triggered to start
		 * transmission.
		 */
		disable_sleep(SLEEP_MASK_UART);

		STM32_USART_CR1(base) |= STM32_USART_CR1_TXEIE;
	} else {
		/*
		 * The TX queue is empty, disable the TXE interrupt and enable
		 * deep sleep mode. The TXE interrupt will remain disabled
		 * until a write call happens.
		 */
		enable_sleep(SLEEP_MASK_UART);

		STM32_USART_CR1(base) &= ~STM32_USART_CR1_TXEIE;
	}
}

static void usart_interrupt_rx(struct usart_config const *config)
{
	intptr_t base = config->hw->base;
	uint8_t  byte = STM32_USART_RDR(base);

	if (!producer_write_unit(&config->producer, &byte))
		atomic_add((uint32_t *) &config->state->rx_dropped, 1);
}

void usart_interrupt(struct usart_config const *config)
{
	intptr_t base = config->hw->base;

	if (STM32_USART_SR(base) & STM32_USART_SR_TXE)
		usart_interrupt_tx(config);

	if (STM32_USART_SR(base) & STM32_USART_SR_RXNE)
		usart_interrupt_rx(config);
}
