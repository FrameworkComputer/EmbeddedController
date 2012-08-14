/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map and API for STM32 processor dma registers
 */

#ifndef __STM32_DMA
#define __STM32_DMA

#include "common.h"

/*
 * Available DMA channels, numbered from 0
 *
 * Note: The STM datasheet tends to number things from 1. We should ask
 * the European elevator engineers to talk to MCU engineer counterparts
 * about this.  This means that if the datasheet refers to channel n,
 * you need to use n-1 in the code.
 */
enum {
	DMAC_ADC,
	DMAC_SPI1_RX,
	DMAC_SPI1_TX,
	DMAC_SPI2_RX,
	DMAC_SPI2_TX,

	/*
	 * The same channels are used for i2c and spi, you can't use them at
	 * the same time or it will cause dma to not work
	 */
	DMAC_I2C_RX = 4,
	DMAC_I2C_TX = 3,

	/* DMA1 has 7 channels, DMA2 has 5 */
	DMA1_NUM_CHANNELS = 7,
	DMA2_NUM_CHANNELS = 5,
	DMA_NUM_CHANNELS = DMA1_NUM_CHANNELS + DMA2_NUM_CHANNELS,
};

/* A single channel of the DMA controller */
struct dma_channel {
	uint32_t	ccr;		/* Control */
	uint32_t	cndtr;		/* Number of data to transfer */
	uint32_t	cpar;		/* Peripheral address */
	uint32_t	cmar;		/* Memory address */
	uint32_t	reserved;
};

/* Registers for the DMA controller */
struct dma_ctlr {
	uint32_t	isr;
	uint32_t	ifcr;
	struct dma_channel chan[DMA_NUM_CHANNELS];
};

/* Defines for accessing DMA ccr */
#define DMA_PL_SHIFT		12
#define DMA_PL_MASK		(3 << DMA_PL_SHIFT)
enum {
	DMA_PL_LOW,
	DMA_PL_MEDIUM,
	DMA_PL_HIGH,
	DMA_PL_VERY_HIGH,
};

#define DMA_EN			(1 << 0)
#define DMA_TCIE		(1 << 1)
#define DMA_HTIE		(1 << 2)
#define DMA_TEIE		(1 << 3)
#define DMA_DIR_FROM_MEM_MASK	(1 << 4)
#define DMA_MINC_MASK		(1 << 7)
#define DMA_TCIF(channel)	(1 << (1 + 4 * channel))

#define DMA_POLLING_INTERVAL_US	100	/* us */
#define DMA_TRANSFER_TIMEOUT_US	100000	/* us */

/*
 * Certain DMA channels must be used for certain peripherals and transfer
 * directions. We provide an easy way for drivers to select the correct
 * channel.
 */

/**
 * @param spi	SPI port to request: STM32_SPI1_PORT or STM32_SPI2_PORT
 * @return DMA channel to use for rx / tx on that port
 */
#define DMA_CHANNEL_FOR_SPI_RX(spi) \
	((spi) == STM32_SPI1_PORT ? DMAC_SPI1_RX : DMAC_SPI2_RX)
#define DMA_CHANNEL_FOR_SPI_TX(spi) \
	((spi) == STM32_SPI1_PORT ? DMAC_SPI1_TX : DMAC_SPI2_TX)

/**
 * Get a pointer to a DMA channel.
 *
 * @param channel	Channel number to read (DMAC_...)
 * @return pointer to DMA channel registers
 */
struct dma_channel *dma_get_channel(int channel);

/**
 * Prepare a DMA transfer to transmit data from memory to a peripheral
 *
 * Call dma_go() afterwards to actually start the transfer.
 *
 * @param chan		Channel to prepare (use dma_get_channel())
 * @param count		Number of bytes to transfer
 * @param periph	Pointer to peripheral data register
 * @param memory	Pointer to memory address
 * @return pointer to prepared channel
 */
void dma_prepare_tx(struct dma_channel *chan, unsigned count,
		    void *periph, const void *memory);

/**
 * Start a DMA transfer to receive data to memory from a peripheral
 *
 * @param channel	Channel number to read (DMAC_...)
 * @param count		Number of bytes to transfer
 * @param periph	Pointer to peripheral data register
 * @param memory	Pointer to memory address
 */
int dma_start_rx(unsigned channel, unsigned count, void *periph,
		 const void *memory);

/**
 * Stop a DMA transfer on a channel
 *
 * Disable the DMA channel and immediate stop all transfers on it.
 *
 * @param channel	Channel number to stop (DMAC_...)
 */
void dma_disable(unsigned channel);

/**
 * Get the number of bytes available to read, or number of bytes written
 *
 * Since the DMA controller counts downwards, if we know the starting value
 * we can work out how many bytes have been completed so far.
 *
 * @param chan		DMA channel to check (use dma_get_channel())
 * @param orig_count	Original number of bytes requested on channel
 * @return number of bytes completed on a channel, or 0 if this channel is
 *		not enabled
 */
int dma_bytes_done(struct dma_channel *chan, int orig_count);

/**
 * Start a previously-prepared dma channel
 *
 * @param chan	Channel to start (returned from dma_prepare...())
 */
void dma_go(struct dma_channel *chan);

/**
 * Testing: Print out the data transferred by a channel
 *
 * @param channel	Channel number to read (DMAC_...)
 * @param buff		Start of DMA buffer
 */
void dma_check(int channel, char *buff);

/**
 * Dump out imformation about a dma channel
 *
 * @param channel	Channel number to read (DMAC_...)
 */
void dma_dump(unsigned channel);

/**
 * Testing: Test that DMA works correctly for memory to memory transfers
 */
void dma_test(void);

/**
 * Init DMA peripheral ready for use
 */
void dma_init(void);

/**
 * Clear the DMA interrupt/event flags for a given channel
 *
 * @param channel	Which channel's isr to clear (DMAC_...)
 */
void dma_clear_isr(int channel);

/**
 * Enable "Transfer Complete" interrupt for a DMA channel
 *
 * @param channel	Which channel's interrupts to change (DMAC_...)
 */
void dma_enable_tc_interrupt(int channel);

/**
 * Disable "Transfer Complete" interrupt for a DMA channel
 *
 * @param channel	Which channel's interrupts to change (DMAC_...)
 */
void dma_disable_tc_interrupt(int channel);

/**
 * Get a pointer to the DMA peripheral controller that owns the channel
 *
 * @param channel	Channel number to get the controller for (DMAC_...)
 * @return pointer to DMA channel registers
 */
struct dma_ctlr *dma_get_ctlr(int channel);

#endif
