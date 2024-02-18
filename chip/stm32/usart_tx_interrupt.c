/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupt based USART TX driver for STM32 */

#include "common.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "usart.h"
#include "usart_host_command.h"
#include "util.h"

typedef size_t (*remove_data_t)(struct usart_config const *config,
				uint8_t *dest);

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

static void usart_tx_interrupt_handler_common(struct usart_config const *config,
					      remove_data_t remove_data)
{
	intptr_t base = config->hw->base;
	uint8_t byte;

	if (!(STM32_USART_SR(base) & STM32_USART_SR_TXE))
		return;

	while (remove_data(config, &byte)) {
		STM32_USART_TDR(base) = byte;

		/*
		 * Make sure the TXE interrupt is enabled and that we won't go
		 * into deep sleep.  This invocation of the USART interrupt
		 * handler may have been manually triggered to start
		 * transmission.
		 */
		disable_sleep(SLEEP_MASK_UART);

		STM32_USART_CR1(base) |= STM32_USART_CR1_TXEIE;

#ifdef STM32_USART_CR1_FIFOEN
		/*
		 * UART has FIFO, see if there is more room.  (TXE has the
		 * meaning of "TX fifo not full", when fifo is enabled.)
		 */
		if (!(STM32_USART_SR(base) & STM32_USART_SR_TXE))
			return;
#else
		/* Do not loop. */
		return;
#endif
	}
	/*
	 * The TX queue is empty, disable the TXE interrupt and enable deep
	 * sleep mode. The TXE interrupt will remain disabled until a write call
	 * happens.
	 */
	enable_sleep(SLEEP_MASK_UART);

	STM32_USART_CR1(base) &= ~STM32_USART_CR1_TXEIE;
}

static size_t queue_remove(struct usart_config const *config, uint8_t *dest)
{
	return queue_remove_unit(config->consumer.queue, (void *)dest);
}

static void usart_tx_interrupt_handler(struct usart_config const *config)
{
	usart_tx_interrupt_handler_common(config, &queue_remove);
}

void usart_tx_start(struct usart_config const *config)
{
	intptr_t base = config->hw->base;

	/* If interrupt is already enabled, nothing to do */
	if (STM32_USART_CR1(base) & STM32_USART_CR1_TXEIE)
		return;

	disable_sleep(SLEEP_MASK_UART);
	STM32_USART_CR1(base) |= (STM32_USART_CR1_TXEIE);

	task_trigger_irq(config->hw->irq);
}

struct usart_tx const usart_tx_interrupt = {
	.consumer_ops = {
		.written = usart_written,
	},

	.init      = usart_tx_init,
	.interrupt = usart_tx_interrupt_handler,
	.info      = NULL,
};

#if defined(CONFIG_USART_HOST_COMMAND)

static void
usart_host_command_tx_interrupt_handler(struct usart_config const *config)
{
	usart_tx_interrupt_handler_common(config,
					  &usart_host_command_tx_remove_data);
}

struct usart_tx const usart_host_command_tx_interrupt = {
	.consumer_ops = {
		.written = usart_written,
	},

	.init      = usart_tx_init,
	.interrupt = usart_host_command_tx_interrupt_handler,
	.info      = NULL,
};
#endif /* CONFIG_USART_HOST_COMMAND */
