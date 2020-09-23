/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usart_rx_dma.h"

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "registers.h"
#include "system.h"
#include "usart_host_command.h"
#include "util.h"

typedef size_t (*add_data_t)(struct usart_config const *config,
	const uint8_t *src, size_t count);

void usart_rx_dma_init(struct usart_config const *config)
{
	struct usart_rx_dma const *dma_config =
		DOWNCAST(config->rx, struct usart_rx_dma const, usart_rx);

	intptr_t base = config->hw->base;

	struct dma_option options = {
		.channel = dma_config->channel,
		.periph  = (void *)&STM32_USART_RDR(base),
		.flags   = (STM32_DMA_CCR_MSIZE_8_BIT |
			    STM32_DMA_CCR_PSIZE_8_BIT |
			    STM32_DMA_CCR_CIRC),
	};

	if (IS_ENABLED(CHIP_FAMILY_STM32F4))
		options.flags |= STM32_DMA_CCR_CHANNEL(STM32_REQ_USART1_RX);

	STM32_USART_CR1(base) |= STM32_USART_CR1_RXNEIE;
	STM32_USART_CR1(base) |= STM32_USART_CR1_RE;
	STM32_USART_CR3(base) |= STM32_USART_CR3_DMAR;

	dma_config->state->index     = 0;
	dma_config->state->max_bytes = 0;

	dma_start_rx(&options, dma_config->fifo_size, dma_config->fifo_buffer);
}

static void usart_rx_dma_interrupt_common(
		struct usart_config const *config,
		add_data_t add_data)
{
	struct usart_rx_dma const *dma_config =
		DOWNCAST(config->rx, struct usart_rx_dma const, usart_rx);

	dma_chan_t *channel  = dma_get_channel(dma_config->channel);
	size_t     new_index = dma_bytes_done(channel, dma_config->fifo_size);
	size_t     old_index = dma_config->state->index;
	size_t     new_bytes = 0;
	size_t     added     = 0;

	if (new_index > old_index) {
		new_bytes = new_index - old_index;

		added = add_data(config,
				dma_config->fifo_buffer + old_index,
				new_bytes);
	} else if (new_index < old_index) {
		/*
		 * Handle the case where the received bytes are not contiguous
		 * in the circular DMA buffer.  This is done with two queue
		 * adds.
		 */
		new_bytes = dma_config->fifo_size - (old_index - new_index);

		added = add_data(config,
				dma_config->fifo_buffer + old_index,
				dma_config->fifo_size - old_index) +
			add_data(config,
				dma_config->fifo_buffer,
				new_index);
	} else {
		/* (new_index == old_index): nothing to add to the queue. */
	}

	deprecated_atomic_add(&config->state->rx_dropped, new_bytes - added);

	if (dma_config->state->max_bytes < new_bytes)
		dma_config->state->max_bytes = new_bytes;

	dma_config->state->index = new_index;
}

static size_t queue_add(struct usart_config const *config,
			const uint8_t *src, size_t count)
{
	return queue_add_units(config->producer.queue, (void *)src, count);
}

void usart_rx_dma_interrupt(struct usart_config const *config)
{
	usart_rx_dma_interrupt_common(config, &queue_add);
}


#if defined(CONFIG_USART_HOST_COMMAND)
void usart_host_command_rx_dma_interrupt(struct usart_config const *config)
{
	usart_rx_dma_interrupt_common(config,
				      &usart_host_command_rx_append_data);
}
#endif /* CONFIG_USART_HOST_COMMAND */

void usart_rx_dma_info(struct usart_config const *config)
{
	struct usart_rx_dma const *dma_config =
		DOWNCAST(config->rx, struct usart_rx_dma const, usart_rx);

	ccprintf("    DMA RX max_bytes %d\n",
		 deprecated_atomic_read_clear(&dma_config->state->max_bytes));
}
