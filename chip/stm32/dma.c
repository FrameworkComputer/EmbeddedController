/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "dma.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_DMA, outstr)
#define CPRINTS(format, args...) cprints(CC_DMA, format, ## args)

/* Task IDs for the interrupt handlers to wake up */
static task_id_t id[STM32_DMAC_COUNT];

/**
 * Return the IRQ for the DMA channel
 *
 * @param channel	Channel number
 * @return IRQ for the channel
 */
static int dma_get_irq(enum dma_channel channel)
{
	return STM32_IRQ_DMA_CHANNEL_1 + channel;
}

/*
 * Note, you must decrement the channel value by 1 from what is specified
 * in the datasheets, as they index from 1 and this indexes from 0!
 */
stm32_dma_chan_t *dma_get_channel(enum dma_channel channel)
{
	stm32_dma_regs_t *dma = STM32_DMA1_REGS;

	return &dma->chan[channel];
}

void dma_disable(enum dma_channel channel)
{
	stm32_dma_chan_t *chan = dma_get_channel(channel);

	if (chan->ccr & STM32_DMA_CCR_EN)
		chan->ccr &= ~STM32_DMA_CCR_EN;
}

/**
 * Prepare a channel for use and start it
 *
 * @param chan		Channel to read
 * @param count		Number of bytes to transfer
 * @param periph	Pointer to peripheral data register
 * @param memory	Pointer to memory address for receive/transmit
 * @param flags		DMA flags for the control register, normally:
 *				STM32_DMA_CCR_MINC | STM32_DMA_CCR_DIR for tx
 *				0 for rx
 */
static void prepare_channel(stm32_dma_chan_t *chan, unsigned count,
		void *periph, void *memory, unsigned flags)
{
	uint32_t ccr = STM32_DMA_CCR_PL_VERY_HIGH;

	if (chan->ccr & STM32_DMA_CCR_EN)
		chan->ccr &= ~STM32_DMA_CCR_EN;

	/* Following the order in Doc ID 15965 Rev 5 p194 */
	chan->cpar = (uint32_t)periph;
	chan->cmar = (uint32_t)memory;
	chan->cndtr = count;
	chan->ccr = ccr;
	ccr |= flags;
	chan->ccr = ccr;
}

void dma_go(stm32_dma_chan_t *chan)
{
	/* Flush data in write buffer so that DMA can get the lastest data */
	asm volatile("dsb;");

	/* Fire it up */
	chan->ccr |= STM32_DMA_CCR_EN;
}

void dma_prepare_tx(const struct dma_option *option, unsigned count,
		    const void *memory)
{
	stm32_dma_chan_t *chan = dma_get_channel(option->channel);

	/*
	 * Cast away const for memory pointer; this is ok because we know
	 * we're preparing the channel for transmit.
	 */
	prepare_channel(chan, count, option->periph, (void *)memory,
			STM32_DMA_CCR_MINC | STM32_DMA_CCR_DIR |
			option->flags);
}

void dma_start_rx(const struct dma_option *option, unsigned count,
		  void *memory)
{
	stm32_dma_chan_t *chan = dma_get_channel(option->channel);

	prepare_channel(chan, count, option->periph, memory,
			STM32_DMA_CCR_MINC | option->flags);
	dma_go(chan);
}

int dma_bytes_done(stm32_dma_chan_t *chan, int orig_count)
{
	if (!(chan->ccr & STM32_DMA_CCR_EN))
		return 0;
	return orig_count - chan->cndtr;
}

#ifdef CONFIG_DMA_HELP
void dma_dump(enum dma_channel channel)
{
	stm32_dma_regs_t *dma = STM32_DMA1_REGS;
	stm32_dma_chan_t *chan = dma_get_channel(channel);

	CPRINTF("ccr=%x, cndtr=%x, cpar=%x, cmar=%x\n", chan->ccr,
		chan->cndtr, chan->cpar, chan->cmar);
	CPRINTF("chan %d, isr=%x, ifcr=%x\n",
		channel,
		(dma->isr >> (channel * 4)) & 0xf,
		(dma->ifcr >> (channel * 4)) & 0xf);
}

void dma_check(enum dma_channel channel, char *buf)
{
	stm32_dma_chan_t *chan;
	int count;
	int i;

	chan = dma_get_channel(channel);
	count = chan->cndtr;
	CPRINTF("c=%d\n", count);
	udelay(100 * MSEC);
	CPRINTF("c=%d\n", chan->cndtr);
	for (i = 0; i < count; i++)
		CPRINTF("%02x ", buf[i]);
	udelay(100 * MSEC);
	CPRINTF("c=%d\n", chan->cndtr);
	for (i = 0; i < count; i++)
		CPRINTF("%02x ", buf[i]);
}

/* Run a check of memory-to-memory DMA */
void dma_test(void)
{
	enum dma_channel channel = STM32_DMAC_CH4;
	stm32_dma_chan_t *chan = dma_get_channel(channel);
	uint32_t ctrl;
	char periph[16], memory[16];
	unsigned count = sizeof(periph);
	int i;

	memset(memory, '\0', sizeof(memory));
	for (i = 0; i < count; i++)
		periph[i] = 10 + i;

	/* Following the order in Doc ID 15965 Rev 5 p194 */
	chan->cpar = (uint32_t)periph;
	chan->cmar = (uint32_t)memory;
	chan->cndtr = count;
	ctrl = STM32_DMA_CCR_PL_MEDIUM;
	chan->ccr = ctrl;

	ctrl |= STM32_DMA_CCR_MINC; /* | STM32_DMA_CCR_DIR */;
	ctrl |= STM32_DMA_CCR_MEM2MEM;
	ctrl |= STM32_DMA_CCR_PINC;
/*	ctrl |= STM32_DMA_CCR_MSIZE_32_BIT; */
/*	ctrl |= STM32_DMA_CCR_PSIZE_32_BIT; */
	chan->ccr = ctrl;
	chan->ccr = ctrl | STM32_DMA_CCR_EN;

	for (i = 0; i < count; i++)
		CPRINTF("%d/%d ", periph[i], memory[i]);
	CPRINTF("\ncount=%d\n", chan->cndtr);
}
#endif /* CONFIG_DMA_HELP */

void dma_init(void)
{
	int i;

	/* Enable DMA1; current chips don't have DMA2 */
	STM32_RCC_AHBENR |= STM32_RCC_HB_DMA1;

	/* Initialize data for interrupt handlers */
	for (i = 0; i < STM32_DMAC_COUNT; i++)
		id[i] = TASK_ID_INVALID;
}

int dma_wait(enum dma_channel channel)
{
	stm32_dma_regs_t *dma = STM32_DMA1_REGS;
	const uint32_t mask = STM32_DMA_ISR_TCIF(channel);
	timestamp_t deadline;

	deadline.val = get_time().val + DMA_TRANSFER_TIMEOUT_US;
	while ((dma->isr & mask) != mask) {
		if (deadline.val <= get_time().val)
			return EC_ERROR_TIMEOUT;

		udelay(DMA_POLLING_INTERVAL_US);
	}
	return EC_SUCCESS;
}

void dma_enable_tc_interrupt(enum dma_channel channel)
{
	stm32_dma_chan_t *chan = dma_get_channel(channel);

	/* Store task ID so the ISR knows which task to wake */
	id[channel] = task_get_current();

	chan->ccr |= STM32_DMA_CCR_TCIE;
	task_enable_irq(dma_get_irq(channel));
}

void dma_disable_tc_interrupt(enum dma_channel channel)
{
	stm32_dma_chan_t *chan = dma_get_channel(channel);

	id[channel] = TASK_ID_INVALID;

	chan->ccr &= ~STM32_DMA_CCR_TCIE;
	task_disable_irq(dma_get_irq(channel));
}

void dma_clear_isr(enum dma_channel channel)
{
	stm32_dma_regs_t *dma = STM32_DMA1_REGS;

	dma->ifcr |= STM32_DMA_ISR_ALL(channel);
}

#ifndef CHIP_FAMILY_STM32F0
void dma_event_interrupt_channel_4(void)
{
	dma_clear_isr(STM32_DMAC_CH4);
	if (id[STM32_DMAC_CH4] != TASK_ID_INVALID)
		task_wake(id[STM32_DMAC_CH4]);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_4, dma_event_interrupt_channel_4, 3);

void dma_event_interrupt_channel_5(void)
{
	dma_clear_isr(STM32_DMAC_CH5);
	if (id[STM32_DMAC_CH5] != TASK_ID_INVALID)
		task_wake(id[STM32_DMAC_CH5]);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_5, dma_event_interrupt_channel_5, 3);

void dma_event_interrupt_channel_6(void)
{
	dma_clear_isr(STM32_DMAC_CH6);
	if (id[STM32_DMAC_CH6] != TASK_ID_INVALID)
		task_wake(id[STM32_DMAC_CH6]);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_6, dma_event_interrupt_channel_6, 3);

void dma_event_interrupt_channel_7(void)
{
	dma_clear_isr(STM32_DMAC_CH7);
	if (id[STM32_DMAC_CH7] != TASK_ID_INVALID)
		task_wake(id[STM32_DMAC_CH7]);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_7, dma_event_interrupt_channel_7, 3);
#endif /* !CHIP_FAMILY_STM32F0 */
