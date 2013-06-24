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
#define CPRINTF(format, args...) cprintf(CC_DMA, format, ## args)

/* Task IDs for the interrupt handlers to wake up */
static task_id_t id[DMA_NUM_CHANNELS];

/* Registers for the DMA controller */
struct dma_ctlr {
	uint32_t	isr;
	uint32_t	ifcr;
	dma_channel_t chan[DMA_NUM_CHANNELS];
};

/* Always use dma_ctlr_t so volatile keyword is included! */
typedef volatile struct dma_ctlr dma_ctlr_t;

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

/**
 * Get a pointer to the DMA peripheral controller that owns the channel
 *
 * @param channel	Channel number to get the controller for (DMAC_...)
 * @return pointer to DMA channel registers
 */
static dma_ctlr_t *dma_get_ctlr(void)
{
	return (dma_ctlr_t *)STM32_DMA1_BASE;
}

/*
 * Note, you must decrement the channel value by 1 from what is specified
 * in the datasheets, as they index from 1 and this indexes from 0!
 */
dma_channel_t *dma_get_channel(enum dma_channel channel)
{
	dma_ctlr_t *dma = dma_get_ctlr();

	/* Get a pointer to the correct controller and channel */
	ASSERT(channel < DMA_NUM_CHANNELS);

	return &dma->chan[channel];
}

void dma_disable(enum dma_channel channel)
{
	dma_channel_t *chan = dma_get_channel(channel);

	if (chan->ccr & DMA_EN)
		chan->ccr &= ~DMA_EN;
}

/**
 * Prepare a channel for use and start it
 *
 * @param chan		Channel number to read (DMAC_...)
 * @param count		Number of bytes to transfer
 * @param periph	Pointer to peripheral data register
 * @param memory	Pointer to memory address for receive/transmit
 * @param flags		DMA flags for the control register, normally:
				DMA_MINC_MASK |
 *				(DMA_DIR_FROM_MEM_MASK for tx
 *				0 for rx)
 */
static void prepare_channel(dma_channel_t *chan, unsigned count,
		void *periph, void *memory, unsigned flags)
{
	uint32_t ctrl = DMA_PL_VERY_HIGH << DMA_PL_SHIFT;

	if (chan->ccr & DMA_EN)
		chan->ccr &= ~DMA_EN;

	/* Following the order in Doc ID 15965 Rev 5 p194 */
	chan->cpar = (uint32_t)periph;
	chan->cmar = (uint32_t)memory;
	chan->cndtr = count;
	chan->ccr = ctrl;

	ctrl |= flags;
	chan->ccr = ctrl;
}

void dma_go(dma_channel_t *chan)
{
	/* Fire it up */
	chan->ccr |= DMA_EN;
}

void dma_prepare_tx(const struct dma_option *option, unsigned count,
		    const void *memory)
{
	dma_channel_t *chan = dma_get_channel(option->channel);

	/*
	 * Cast away const for memory pointer; this is ok because we know
	 * we're preparing the channel for transmit.
	 */
	prepare_channel(chan, count, option->periph, (void *)memory,
			DMA_MINC_MASK | DMA_DIR_FROM_MEM_MASK | option->flags);
}

void dma_start_rx(const struct dma_option *option, unsigned count,
		  void *memory)
{
	dma_channel_t *chan = dma_get_channel(option->channel);

	prepare_channel(chan, count, option->periph, memory,
			DMA_MINC_MASK | option->flags);
	dma_go(chan);
}

int dma_bytes_done(dma_channel_t *chan, int orig_count)
{
	if (!(chan->ccr & DMA_EN))
		return 0;
	return orig_count - chan->cndtr;
}

#ifdef CONFIG_DMA_HELP
void dma_dump(enum dma_channel channel)
{
	dma_ctlr_t *dma = dma_get_ctlr();
	dma_channel_t *chan = dma_get_channel(channel);

	CPRINTF("ccr=%x, cndtr=%x, cpar=%x, cmar=%x\n", chan->ccr,
		chan->cndtr, chan->cpar, chan->cmar);
	CPRINTF("chan %d, isr=%x, ifcr=%x\n",
		channel,
		(dma->isr >> (channel * 4)) & 0xf,
		(dma->ifcr >> (channel * 4)) & 0xf);
}

void dma_check(enum dma_channel channel, char *buf)
{
	dma_channel_t *chan;
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
	dma_channel_t *chan = dma_get_channel(channel);
	unsigned channel = 3;
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
	ctrl = DMA_PL_MEDIUM << DMA_PL_SHIFT;
	chan->ccr = ctrl;

	ctrl |= DMA_MINC_MASK; /* | DMA_DIR_FROM_MEM_MASK */;
	ctrl |= 1 << 14; /* MEM2MEM */
	ctrl |= 1 << 6;  /* PINC */
/*	ctrl |= 2 << 10; */
/*	ctrl |= 2 << 8; */
	chan->ccr = ctrl;

	ctrl |= DMA_EN;
	chan->ccr = ctrl;
	for (i = 0; i < count; i++)
		CPRINTF("%d/%d ", periph[i], memory[i]);
	CPRINTF("\ncount=%d\n", chan->cndtr);
}
#endif /* CONFIG_DMA_HELP */

static void dma_init(void)
{
	int i;

	/* Enable DMA1; current chips don't have DMA2 */
	STM32_RCC_AHBENR |= RCC_AHBENR_DMA1EN;

	/* Initialize data for interrupt handlers */
	for (i = 0; i < DMA_NUM_CHANNELS; i++)
		id[i] = TASK_ID_INVALID;
}
DECLARE_HOOK(HOOK_INIT, dma_init, HOOK_PRIO_INIT_DMA);

int dma_wait(enum dma_channel channel)
{
	dma_ctlr_t *dma = dma_get_ctlr();
	uint32_t mask = DMA_TCIF(channel);
	timestamp_t deadline;

	deadline.val = get_time().val + DMA_TRANSFER_TIMEOUT_US;
	while ((dma->isr & mask) != mask) {
		if (deadline.val <= get_time().val)
			return EC_ERROR_TIMEOUT;
		else
			udelay(DMA_POLLING_INTERVAL_US);
	}
	return EC_SUCCESS;
}

void dma_enable_tc_interrupt(enum dma_channel channel)
{
	dma_channel_t *chan = dma_get_channel(channel);

	/* Store task ID so the ISR knows which task to wake */
	id[channel] = task_get_current();

	chan->ccr |= DMA_TCIE;
	task_enable_irq(dma_get_irq(channel));
}

void dma_disable_tc_interrupt(enum dma_channel channel)
{
	dma_channel_t *chan = dma_get_channel(channel);

	id[channel] = TASK_ID_INVALID;

	chan->ccr &= ~DMA_TCIE;
	task_disable_irq(dma_get_irq(channel));
}

void dma_clear_isr(enum dma_channel channel)
{
	dma_ctlr_t *dma = dma_get_ctlr();

	dma->ifcr |= 0x0f << (4 * channel);
}

static void dma_event_interrupt_channel_4(void)
{
	dma_clear_isr(DMAC_CH4);
	if (id[DMAC_CH4] != TASK_ID_INVALID)
		task_wake(id[DMAC_CH4]);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_4, dma_event_interrupt_channel_4, 3);

static void dma_event_interrupt_channel_5(void)
{
	dma_clear_isr(DMAC_CH5);
	if (id[DMAC_CH5] != TASK_ID_INVALID)
		task_wake(id[DMAC_CH5]);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_5, dma_event_interrupt_channel_5, 3);

static void dma_event_interrupt_channel_6(void)
{
	dma_clear_isr(DMAC_CH6);
	if (id[DMAC_CH6] != TASK_ID_INVALID)
		task_wake(id[DMAC_CH6]);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_6, dma_event_interrupt_channel_6, 3);

static void dma_event_interrupt_channel_7(void)
{
	dma_clear_isr(DMAC_CH7);
	if (id[DMAC_CH7] != TASK_ID_INVALID)
		task_wake(id[DMAC_CH7]);
}
DECLARE_IRQ(STM32_IRQ_DMA_CHANNEL_7, dma_event_interrupt_channel_7, 3);
