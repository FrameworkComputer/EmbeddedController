/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * DMA interface
 */

#ifndef __CROS_EC_DMA_H
#define __CROS_EC_DMA_H

#ifdef CONFIG_DMA

#include "common.h"
#include "registers.h"

/* DMA channel options */
struct dma_option {
	enum dma_channel channel;	/* DMA channel */
	void *periph;		/* Pointer to peripheral data register */
	unsigned flags;		/* DMA flags for the control register. Normally
				   used to select memory size. */
};

#define DMA_POLLING_INTERVAL_US	100	/* us */
#define DMA_TRANSFER_TIMEOUT_US	(100 * MSEC) /* us */

/**
 * Get a pointer to a DMA channel.
 *
 * @param channel	Channel to read
 * @return pointer to DMA channel registers
 */
dma_chan_t *dma_get_channel(enum dma_channel channel);

/**
 * Prepare a DMA transfer to transmit data from memory to a peripheral
 *
 * Call dma_go() afterwards to actually start the transfer.
 *
 * @param option	DMA channel options
 * @param count		Number of bytes to transfer
 * @param memory	Pointer to memory address
 *
 * @return pointer to prepared channel
 */
void dma_prepare_tx(const struct dma_option *option, unsigned count,
		    const void *memory);

/**
 * Start a DMA transfer to receive data to memory from a peripheral
 *
 * @param option	DMA channel options
 * @param count		Number of bytes to transfer
 * @param memory	Pointer to memory address
 */
void dma_start_rx(const struct dma_option *option, unsigned count,
		  void *memory);

/**
 * Stop a DMA transfer on a channel
 *
 * Disable the DMA channel and immediate stop all transfers on it.
 *
 * @param channel	Channel to stop
 */
void dma_disable(enum dma_channel channel);

/* Stop transfers on all DMA channels */
void dma_disable_all(void);

/**
 * Get the number of bytes available to read, or number of bytes written
 *
 * Since the DMA controller counts downwards, if we know the starting value
 * we can work out how many bytes have been completed so far.
 *
 * @param chan		DMA channel to check, from dma_get_channel()
 * @param orig_count	Original number of bytes requested on channel
 * @return number of bytes completed on a channel, or 0 if this channel is
 *		not enabled
 */
int dma_bytes_done(dma_chan_t *chan, int orig_count);

/**
 * Start a previously-prepared dma channel
 *
 * @param chan	Channel to start, from dma_get_channel()
 */
void dma_go(dma_chan_t *chan);

#ifdef CONFIG_DMA_HELP
/**
 * Testing: Print out the data transferred by a channel
 *
 * @param channel	Channel to read
 * @param buff		Start of DMA buffer
 */
void dma_check(enum dma_channel channel, char *buf);

/**
 * Dump out imformation about a dma channel
 *
 * @param channel	Channel to read
 */
void dma_dump(enum dma_channel channel);

/**
 * Testing: Test that DMA works correctly for memory to memory transfers
 */
void dma_test(void);
#endif  /* CONFIG_DMA_HELP */

/**
 * Clear the DMA interrupt/event flags for a given channel
 *
 * @param channel	Which channel's isr to clear
 */
void dma_clear_isr(enum dma_channel channel);

/**
 * Enable "Transfer Complete" interrupt for a DMA channel.
 * Will Wake up calling task when complete.
 *
 * @param channel	Which channel's interrupts to change
 */
void dma_enable_tc_interrupt(enum dma_channel channel);

/**
 * Enable "Transfer Complete" interrupt for a DMA channel with callback
 * NOTE: The callback is run at highest interrupt priority so should be
 * fast and not depend on get_time().
 *
 * @param channel	Which channel's interrupts to change
 * @param callback	Pointer to callback function to call on interrupt
 * @param callback_data Data to pass through to callback function
 */
void dma_enable_tc_interrupt_callback(enum dma_channel channel,
				      void (*callback)(void *),
				      void *callback_data);

/**
 * Disable "Transfer Complete" interrupt for a DMA channel
 *
 * @param channel	Which channel's interrupts to change
 */
void dma_disable_tc_interrupt(enum dma_channel channel);

/**
 * Wait for the DMA transfer to complete by polling the transfer complete flag
 *
 * @param channel	Channel number to wait on
 * @return EC_ERROR_TIMEOUT for timeout, EC_SUCCESS for success
 */
int dma_wait(enum dma_channel channel);

/**
 * Initialize the DMA module.
 */
void dma_init(void);

#endif /* CONFIG_DMA */
#endif
