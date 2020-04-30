/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Hybrid DMA/Interrupt based USART RX driver for STM32
 */
#ifndef __CROS_EC_USART_RX_DMA_H
#define __CROS_EC_USART_RX_DMA_H

#include "producer.h"
#include "dma.h"
#include "queue.h"
#include "usart.h"

/*
 * Only reference the usart_rx_dma_info function if CONFIG_CMD_USART_INFO
 * is defined.  This allows the compiler to remove this function as dead code
 * when CONFIG_CMD_USART_INFO is not defined.
 */
#ifdef CONFIG_CMD_USART_INFO
#define USART_RX_DMA_INFO usart_rx_dma_info
#else
#define USART_RX_DMA_INFO NULL
#endif

/*
 * Construct a USART RX instance for DMA using the given DMA channel.
 *
 * This macro creates a new usart_rx_dma struct, complete with in RAM state,
 * the contained usart_rx struct can be used in initializing a usart_config
 * struct.
 *
 * CHANNEL is the DMA channel to be used for reception.  This must be a valid
 * DMA channel for the USART peripheral and any alternate channel mappings must
 * be handled by the board specific code.
 *
 * FIFO_SIZE is the number of bytes (which does not need to be a power of two)
 * to use for the DMA circular buffer.  This buffer must be large enough to
 * hide the worst case interrupt latency the system will encounter.  The DMA
 * RX driver adds to the output of the usart_info command a high water mark
 * of how many bytes were transferred out of this FIFO on any one interrupt.
 * This value can be used to correctly size the FIFO by setting the FIFO_SIZE
 * to something large, stress test the USART, and run usart_info.  After a
 * reasonable stress test the "DMA RX max_bytes" value will be a reasonable
 * size for the FIFO (perhaps +10% for safety).
 */
#define USART_RX_DMA(CHANNEL, FIFO_SIZE)				\
	((struct usart_rx_dma const) {					\
		.usart_rx = {						\
			.producer_ops = {				\
				.read = NULL,				\
			},						\
									\
			.init      = usart_rx_dma_init,			\
			.interrupt = usart_rx_dma_interrupt,		\
			.info      = USART_RX_DMA_INFO,			\
		},							\
									\
		.state       = &((struct usart_rx_dma_state) {}),	\
		 .fifo_buffer = ((uint8_t[FIFO_SIZE]) {}),		\
		 .fifo_size   = FIFO_SIZE,				\
		 .channel     = CHANNEL,				\
	})

/*
 * In RAM state required to manage DMA based transmission.
 */
struct usart_rx_dma_state {
	/*
	 * Previous value of dma_bytes_done.  This will wrap when the DMA fills
	 * the queue.
	 */
	size_t index;

	/*
	 * Maximum number of bytes transferred in any one RX interrupt.
	 */
	uint32_t max_bytes;
};

/*
 * Extension of the usart_rx struct to include required configuration for
 * DMA based transmission.
 */
struct usart_rx_dma {
	struct usart_rx usart_rx;

	struct usart_rx_dma_state volatile *state;

	uint8_t *fifo_buffer;
	size_t  fifo_size;

	enum dma_channel channel;
};

/*
 * Function pointers needed to initialize a usart_rx struct.  These shouldn't
 * be called in any other context as they assume that the producer or config
 * that they are passed was initialized with a complete usart_rx_dma struct.
 */
void usart_rx_dma_init(struct usart_config const *config);
void usart_rx_dma_interrupt(struct usart_config const *config);

/*
 * Function pointers needed to initialize host command rx dma interrupt.
 * This should be only called from usart host command layer.
 */
void usart_host_command_rx_dma_interrupt(struct usart_config const *config);

/*
 * Debug function, used to print DMA RX statistics to the console.
 */
void usart_rx_dma_info(struct usart_config const *config);

#endif /* __CROS_EC_USART_RX_DMA_H */
