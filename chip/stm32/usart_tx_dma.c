/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usart_tx_dma.h"

#include "usart.h"
#include "common.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"

void usart_tx_dma_written(struct consumer const *consumer, size_t count)
{
	struct usart_config const *config =
		DOWNCAST(consumer, struct usart_config, consumer);

	task_trigger_irq(config->hw->irq);
}

void usart_tx_dma_init(struct usart_config const *config)
{
	struct usart_tx_dma const *dma_config =
		DOWNCAST(config->tx, struct usart_tx_dma const, usart_tx);

	intptr_t base = config->hw->base;

	STM32_USART_CR1(base) |= STM32_USART_CR1_TE;
	STM32_USART_CR3(base) |= STM32_USART_CR3_DMAT;

	dma_config->state->dma_active = 0;
}

static void usart_tx_dma_start(struct usart_config const *config,
			       struct usart_tx_dma const *dma_config)
{
	struct usart_tx_dma_state volatile *state = dma_config->state;
	intptr_t                           base   = config->hw->base;

	struct dma_option options = {
		.channel = dma_config->channel,
		.periph  = (void *)&STM32_USART_TDR(base),
		.flags   = (STM32_DMA_CCR_MSIZE_8_BIT |
			    STM32_DMA_CCR_PSIZE_8_BIT),
	};

	/*
	 * Limit our DMA transfer.  If we didn't do this then it would be
	 * possible to start a large DMA transfer of an entirely full buffer
	 * that would hold up any additional writes to the TX queue
	 * unnecessarily.
	 */
	state->chunk.count = MIN(state->chunk.count, dma_config->max_bytes);

	dma_prepare_tx(&options, state->chunk.count, state->chunk.buffer);

	state->dma_active = 1;

	usart_clear_tc(config);
	STM32_USART_CR1(base) |= STM32_USART_CR1_TCIE;

	dma_go(dma_get_channel(options.channel));
}

static void usart_tx_dma_stop(struct usart_config const *config,
			      struct usart_tx_dma const *dma_config)
{
	dma_config->state->dma_active = 0;

	STM32_USART_CR1(config->hw->base) &= ~STM32_USART_CR1_TCIE;
}

void usart_tx_dma_interrupt(struct usart_config const *config)
{
	struct usart_tx_dma const *dma_config =
		DOWNCAST(config->tx, struct usart_tx_dma const, usart_tx);
	struct usart_tx_dma_state volatile *state = dma_config->state;

	/*
	 * If we have completed a DMA transaction, or if we haven't yet started
	 * one then we clean up and start one now.
	 */
	if ((STM32_USART_SR(config->hw->base) & STM32_USART_SR_TC) ||
	    !state->dma_active) {
		struct queue const *queue = config->consumer.queue;

		/*
		 * Only advance the queue head (indicating that we have read
		 * units from the queue if we had an active DMA transfer.
		 */
		if (state->dma_active)
			queue_advance_head(queue, state->chunk.count);

		state->chunk = queue_get_read_chunk(queue);

		if (state->chunk.count)
			usart_tx_dma_start(config, dma_config);
		else
			usart_tx_dma_stop(config, dma_config);
	}
}
