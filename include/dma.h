/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map and API for STM32 processor dma registers
 */

#ifndef __STM32_DMA
#define __STM32_DMA

#include "common.h"

/*
 * Available DMA channels, numbered from 0.
 *
 * Note: The STM datasheet tends to number things from 1. We should ask
 * the European elevator engineers to talk to MCU engineer counterparts
 * about this.  This means that if the datasheet refers to channel n,
 * you need to use DMAC_CHn (=n-1) in the code.
 *
 * Also note that channels are overloaded; obviously you can only use one
 * function on each channel at a time.
 */
enum dma_channel {
	/* Channel numbers */
	DMAC_CH1 = 0,
	DMAC_CH2 = 1,
	DMAC_CH3 = 2,
	DMAC_CH4 = 3,
	DMAC_CH5 = 4,
	DMAC_CH6 = 5,
	DMAC_CH7 = 6,

	/* Channel functions */
	DMAC_ADC = DMAC_CH1,
	DMAC_SPI1_RX = DMAC_CH2,
	DMAC_SPI1_TX = DMAC_CH3,
	DMAC_I2C2_TX = DMAC_CH4,
	DMAC_I2C2_RX = DMAC_CH5,
	DMAC_USART1_TX = DMAC_CH4,
	DMAC_USART1_RX = DMAC_CH5,
	DMAC_I2C1_TX = DMAC_CH6,
	DMAC_I2C1_RX = DMAC_CH7,

	/* Only DMA1 (with 7 channels) is present on STM32F100 and STM32L151x */
	DMA_NUM_CHANNELS = 7,
};

/* Registers for a single channel of the DMA controller */
struct dma_channel_regs {
	uint32_t	ccr;		/* Control */
	uint32_t	cndtr;		/* Number of data to transfer */
	uint32_t	cpar;		/* Peripheral address */
	uint32_t	cmar;		/* Memory address */
	uint32_t	reserved;
};

/* Always use dma_channel_t so volatile keyword is included! */
typedef volatile struct dma_channel_regs dma_channel_t;

/* DMA channel options */
struct dma_option {
	enum dma_channel channel;	/* DMA channel */
	void *periph;		/* Pointer to peripheral data register */
	unsigned flags;		/* DMA flags for the control register. Normally
				   used to select memory size. */
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

#define DMA_MSIZE_BYTE		(0 << 10)
#define DMA_MSIZE_HALF_WORD	(1 << 10)
#define DMA_MSIZE_WORD		(2 << 10)

#define DMA_PSIZE_BYTE		(0 << 8)
#define DMA_PSIZE_HALF_WORD	(1 << 8)
#define DMA_PSIZE_WORD		(2 << 8)

#define DMA_POLLING_INTERVAL_US	100	/* us */
#define DMA_TRANSFER_TIMEOUT_US	(100 * MSEC) /* us */

/**
 * Get a pointer to a DMA channel.
 *
 * @param channel	Channel to read
 * @return pointer to DMA channel registers
 */
dma_channel_t *dma_get_channel(enum dma_channel channel);

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
int dma_bytes_done(dma_channel_t *chan, int orig_count);

/**
 * Start a previously-prepared dma channel
 *
 * @param chan	Channel to start, from dma_get_channel()
 */
void dma_go(dma_channel_t *chan);

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
 * Enable "Transfer Complete" interrupt for a DMA channel
 *
 * @param channel	Which channel's interrupts to change
 */
void dma_enable_tc_interrupt(enum dma_channel channel);

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

#endif
