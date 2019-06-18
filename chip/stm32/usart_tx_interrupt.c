/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupt based USART TX driver for STM32 */

#include "usart.h"

#include "common.h"
#include "registers.h"
#include "system.h"
#include "util.h"

static void usart_tx_init(struct usart_config const *config)
{
	intptr_t base = config->hw->base;

	STM32_USART_CR1(base) |= STM32_USART_CR1_TE;
}

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

static void usart_tx_interrupt_handler(struct usart_config const *config)
{
	intptr_t base = config->hw->base;
	uint8_t  byte;

	if (!(STM32_USART_SR(base) & STM32_USART_SR_TXE))
		return;

	if (queue_remove_unit(config->consumer.queue, &byte)) {
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

struct usart_tx const usart_tx_interrupt = {
	.consumer_ops = {
		.written = usart_written,
	},

	.init      = usart_tx_init,
	.interrupt = usart_tx_interrupt_handler,
	.info      = NULL,
};
