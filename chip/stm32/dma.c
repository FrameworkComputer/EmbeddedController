/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dma.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_DMA, outstr)
#define CPRINTF(format, args...) cprintf(CC_DMA, format, ## args)


/**
 * Get a pointer to a DMA channel.
 *
 * @param channel	Channel number to read (DMAC_...)
 * @return pointer to DMA channel registers
 */
static struct dma_channel *get_channel(int channel)
{
	struct dma_channel *chan;
	struct dma_ctlr *dma;

	/* Get a pointer to the correct controller and channel */
	ASSERT(channel < DMA_NUM_CHANNELS);
	if (channel < DMA1_NUM_CHANNELS) {
		dma = (struct dma_ctlr *)STM32_DMA1_BASE;
		chan = &dma->chan[channel];
	} else {
		dma = (struct dma_ctlr *)STM32_DMA2_BASE;
		chan = &dma->chan[channel - DMA1_NUM_CHANNELS];
	}

	return chan;
}

void dma_disable(unsigned channel)
{
	struct dma_channel *chan;

	chan = get_channel(channel);

	if (REG32(&chan->ccr) & DMA_EN)
		REG32(&chan->ccr) &= ~DMA_EN;
}

/**
 * Prepare a channel for use and start it
 *
 * @param channel	Channel number to read (DMAC_...)
 * @param count		Number of bytes to transfer
 * @param periph	Pointer to peripheral data register
 * @param memory	Pointer to memory address
 * @param flags		DMA flags for the control register, normally:
 *				DMA_DIR_FROM_MEM_MASK for tx
 *				0 for rx
 * @return 0 if ok, -1 on error
 */
static int prepare_channel(unsigned channel, unsigned count, void *periph,
		 const void *memory, unsigned flags)
{
	struct dma_channel *chan;
	uint32_t ctrl;

	chan = get_channel(channel);

	if (REG32(&chan->ccr) & DMA_EN)
		REG32(&chan->ccr) &= ~DMA_EN;

	/* Following the order in Doc ID 15965 Rev 5 p194 */
	REG32(&chan->cpar) = (uint32_t)periph;
	REG32(&chan->cmar) = (uint32_t)memory;
	REG32(&chan->cndtr) = count;
	ctrl = DMA_PL_VERY_HIGH << DMA_PL_SHIFT;
	REG32(&chan->ccr) = ctrl;

	ctrl |= DMA_MINC_MASK | flags;
	ctrl |= 0 << 10; /* MSIZE (memory size in bytes) */
	ctrl |= 1 << 8;  /* PSIZE (16-bits for now) */
	REG32(&chan->ccr) = ctrl;

	/* Fire it up */
	ctrl |= DMA_EN;
	REG32(&chan->ccr) = ctrl;

	return 0;
}

int dma_start_tx(unsigned channel, unsigned count, void *periph,
		 const void *memory)
{
	return prepare_channel(channel, count, periph, memory,
				DMA_DIR_FROM_MEM_MASK);
}

int dma_start_rx(unsigned channel, unsigned count, void *periph,
		 const void *memory)
{
	return prepare_channel(channel, count, periph, memory, 0);
}

/* Hide this code behind an undefined CONFIG for now */
#ifdef CONFIG_DMA_TEST

void dma_check(int channel, char *buff)
{
	struct dma_channel *chan;
	int count;
	int i;

	chan = get_channel(channel);
	count = REG32(&chan->cndtr);
	CPRINTF("c=%d\n", count);
	udelay(1000 * 100);
	CPRINTF("c=%d\n",
		    REG32(&chan->cndtr));
	for (i = 0; i < count; i++)
		CPRINTF("%02x ", buff[i]);
	udelay(1000 * 100);
	CPRINTF("c=%d\n",
		    REG32(&chan->cndtr));
	for (i = 0; i < count; i++)
		CPRINTF("%02x ", buff[i]);
}

/* Run a check of memory-to-memory DMA */
void dma_test(void)
{
	unsigned channel = 3;
	struct dma_channel *chan;
	uint32_t ctrl;
	char periph[16], memory[16];
	unsigned count = sizeof(periph);
	int i;

	chan = get_channel(channel);
	memset(memory, '\0', sizeof(memory));
	for (i = 0; i < count; i++)
		periph[i] = 10 + i;

	/* Following the order in Doc ID 15965 Rev 5 p194 */
	REG32(&chan->cpar) = (uint32_t)periph;
	REG32(&chan->cmar) = (uint32_t)memory;
	REG32(&chan->cndtr) = count;
	ctrl = DMA_PL_MEDIUM << DMA_PL_SHIFT;
	REG32(&chan->ccr) = ctrl;

	ctrl |= DMA_MINC_MASK; /* | DMA_DIR_FROM_MEM_MASK */;
	ctrl |= 1 << 14; /* MEM2MEM */
	ctrl |= 1 << 6;  /* PINC */
/*	ctrl |= 2 << 10; */
/*	ctrl |= 2 << 8; */
	REG32(&chan->ccr) = ctrl;

	ctrl |= DMA_EN;
	REG32(&chan->ccr) = ctrl;
	for (i = 0; i < count; i++)
		CPRINTF("%d/%d ", periph[i], memory[i]);
	CPRINTF("\ncount=%d\n", REG32(&chan->cndtr));
}
#endif /* CONFIG_TEST */

void dma_init(void)
{
	/* Enable DMA1, we don't support DMA2 yet */
	STM32_RCC_AHBENR |= 1 << 24;
}
