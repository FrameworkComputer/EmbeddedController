/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * DMA based USART TX driver for STM32
 */
#ifndef __CROS_EC_USART_TX_DMA_H
#define __CROS_EC_USART_TX_DMA_H

#include "consumer.h"
#include "dma.h"
#include "queue.h"
#include "usart.h"

/*
 * Construct a USART TX instance for DMA using the given DMA channel.
 *
 * This macro creates a new usart_tx_dma struct, complete with in RAM state,
 * the contained usart_tx struct can be used in initializing a usart_config
 * struct.
 *
 * CHANNEL is the DMA channel to be used for transmission.  This must be a
 * valid DMA channel for the USART peripheral and any alternate channel
 * mappings must be handled by the board specific code.
 *
 * MAX_BYTES is the maximum size in bytes of a single DMA transfer.  This
 * allows the board to tune how often the TX engine updates the queue state.
 * A larger number here could cause the queue to appear full for longer than
 * required because the queue isn't notified that it has been read from until
 * after the DMA transfer completes.
 */
#define USART_TX_DMA(CHANNEL, MAX_BYTES)			\
	((struct usart_tx_dma const) {				\
		.usart_tx = {					\
			.consumer_ops = {			\
				.written = usart_tx_dma_written,\
			},					\
								\
			.init      = usart_tx_dma_init,		\
			.interrupt = usart_tx_dma_interrupt,	\
			.info      = NULL,			\
		},						\
								\
		.state     = &((struct usart_tx_dma_state){}),	\
		.channel   = CHANNEL,				\
		.max_bytes = MAX_BYTES,				\
	})

/*
 * In RAM state required to manage DMA based transmission.
 */
struct usart_tx_dma_state {
	/*
	 * The current chunk of queue buffer being used for transmission.  Once
	 * the transfer is complete, this is used to update the TX queue head
	 * pointer as well.
	 */
	struct queue_chunk chunk;

	/*
	 * Flag indicating whether a DMA transfer is currently active.
	 */
	int dma_active;
};

/*
 * Extension of the usart_tx struct to include required configuration for
 * DMA based transmission.
 */
struct usart_tx_dma {
	struct usart_tx usart_tx;

	struct usart_tx_dma_state volatile *state;

	enum dma_channel channel;

	size_t max_bytes;
};

/*
 * Function pointers needed to initialize a usart_tx struct.  These shouldn't
 * be called in any other context as they assume that the consumer or config
 * that they are passed was initialized with a complete usart_tx_dma struct.
 */
void usart_tx_dma_written(struct consumer const *consumer, size_t count);
void usart_tx_dma_flush(struct consumer const *consumer);
void usart_tx_dma_init(struct usart_config const *config);
void usart_tx_dma_interrupt(struct usart_config const *config);

#endif /* __CROS_EC_USART_TX_DMA_H */
