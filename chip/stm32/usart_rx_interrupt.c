/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupt based USART RX driver for STM32F0 and STM32F3 */

#include "usart.h"

#include "atomic.h"
#include "common.h"
#include "queue.h"
#include "registers.h"

static void usart_rx_init(struct usart_config const *config)
{
	intptr_t base = config->hw->base;

	STM32_USART_CR1(base) |= STM32_USART_CR1_RXNEIE;
	STM32_USART_CR1(base) |= STM32_USART_CR1_RE;
	STM32_USART_CR3(base) |= STM32_USART_CR3_OVRDIS;
}

static void usart_rx_interrupt_handler(struct usart_config const *config)
{
	intptr_t base   = config->hw->base;
	int32_t  status = STM32_USART_SR(base);

	if (status & STM32_USART_SR_RXNE) {
		uint8_t byte = STM32_USART_RDR(base);

		if (!queue_add_unit(config->producer.queue, &byte))
			deprecated_atomic_add(&config->state->rx_dropped, 1);
	}
}

struct usart_rx const usart_rx_interrupt = {
	.producer_ops = {
		/*
		 * Nothing to do here, we either had enough space in the queue
		 * when a character came in or we dropped it already.
		 */
		.read = NULL,
	},

	.init      = usart_rx_init,
	.interrupt = usart_rx_interrupt_handler,
	.info      = NULL,
};
