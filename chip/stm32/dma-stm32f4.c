/* Copyright 2016 The Chromium OS Authors. All rights reserved.
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
#define CPRINTS(format, args...) cprints(CC_DMA, format, ## args)

stm32_dma_regs_t *STM32_DMA_REGS[] = { STM32_DMA1_REGS, STM32_DMA2_REGS };

/* Callback data to use when IRQ fires */
static struct {
	void (*cb)(void *);	/* Callback function to call */
	void *cb_data;		/* Callback data for callback function */
} dma_irq[STM32_DMAS_TOTAL_COUNT];

/**
 * Return the IRQ for the DMA stream
 *
 * @param stream	stream number
 * @return IRQ for the stream
 */
static int dma_get_irq(enum dma_channel stream)
{
	if (stream < STM32_DMA1_STREAM6)
		return STM32_IRQ_DMA1_STREAM0 + stream;
	if (stream == STM32_DMA1_STREAM7)
		return STM32_IRQ_DMA1_STREAM7;
	if (stream < STM32_DMA2_STREAM5)
		return STM32_IRQ_DMA2_STREAM0 + stream - STM32_DMA2_STREAM0;
	else
		return STM32_IRQ_DMA2_STREAM5 + stream - STM32_DMA2_STREAM5;
}

stm32_dma_regs_t *dma_get_ctrl(enum dma_channel stream)
{
	return STM32_DMA_REGS[stream / STM32_DMAS_COUNT];
}

stm32_dma_stream_t *dma_get_channel(enum dma_channel stream)
{
	stm32_dma_regs_t *dma = dma_get_ctrl(stream);

	return &dma->stream[stream % STM32_DMAS_COUNT];
}

#ifdef CHIP_FAMILY_STM32H7
void dma_select_channel(enum dma_channel channel, uint8_t req)
{
	STM2_DMAMUX_CxCR(DMAMUX1, channel) = req;
}
#endif

void dma_disable(enum dma_channel ch)
{
	stm32_dma_stream_t *stream = dma_get_channel(ch);

	if (stream->scr & STM32_DMA_CCR_EN) {
		stream->scr &= ~STM32_DMA_CCR_EN;
		while (stream->scr & STM32_DMA_CCR_EN)
			;
	}
}

void dma_disable_all(void)
{
	int ch;

	for (ch = 0; ch < STM32_DMAS_TOTAL_COUNT; ch++)
		dma_disable(ch);
}

/**
 * Prepare a stream for use and start it
 *
 * @param stream	stream to read
 * @param count		Number of bytes to transfer
 * @param periph	Pointer to peripheral data register
 * @param memory	Pointer to memory address for receive/transmit
 * @param flags		DMA flags for the control register.
 */
static void prepare_stream(enum dma_channel stream, unsigned count,
		void *periph, void *memory, unsigned flags)
{
	stm32_dma_stream_t *dma_stream = dma_get_channel(stream);
	uint32_t ccr = STM32_DMA_CCR_PL_VERY_HIGH;

	dma_disable(stream);
	dma_clear_isr(stream);

	/* Following the order in DocID026448 Rev 1 (RM0383) p181 */
	dma_stream->spar = (uint32_t)periph;
	dma_stream->sm0ar = (uint32_t)memory;
	dma_stream->sndtr = count;
	dma_stream->scr = ccr;
	ccr |= flags & STM32_DMA_CCR_CHANNEL_MASK;
	dma_stream->scr = ccr;
	dma_stream->sfcr &= ~STM32_DMA_SFCR_DMDIS;
	ccr |= flags;
	dma_stream->scr = ccr;
}

void dma_go(stm32_dma_stream_t *stream)
{
	/* Flush data in write buffer so that DMA can get the latest data */
	asm volatile("dsb;");

	/* Fire it up */
	stream->scr |= STM32_DMA_CCR_EN;
}

void dma_prepare_tx(const struct dma_option *option, unsigned count,
		    const void *memory)
{
	/*
	 * Cast away const for memory pointer; this is ok because we know
	 * we're preparing the stream for transmit.
	 */
	prepare_stream(option->channel, count, option->periph, (void *)memory,
			STM32_DMA_CCR_MINC | STM32_DMA_CCR_DIR_M2P |
			option->flags);
}

void dma_start_rx(const struct dma_option *option, unsigned count,
		  void *memory)
{
	stm32_dma_stream_t *stream = dma_get_channel(option->channel);

	prepare_stream(option->channel, count, option->periph, memory,
			STM32_DMA_CCR_MINC | STM32_DMA_CCR_DIR_P2M |
			option->flags);
	dma_go(stream);
}

int dma_bytes_done(stm32_dma_stream_t *stream, int orig_count)
{
	/*
	 * Note that we're intentionally not checking that DMA is enabled here
	 * because there is a race when the hardware stops the transfer:
	 *
	 * From Section 9.3.14 DMA transfer completion in RM0402 Rev 5
	 * https://www.st.com/resource/en/reference_manual/dm00180369.pdf:
	 * If the stream is configured in non-circular mode, after the end of
	 * the transfer (that is when the number of data to be transferred
	 * reaches zero), the DMA is stopped (EN bit in DMA_SxCR register is
	 * cleared by Hardware) and no DMA request is served unless the software
	 * reprograms the stream and re-enables it (by setting the EN bit in the
	 * DMA_SxCR register).
	 *
	 * See http://b/132444384 for full details.
	 */
	return orig_count - stream->sndtr;
}

bool dma_is_enabled(stm32_dma_stream_t *stream)
{
	return (stream->scr & STM32_DMA_CCR_EN);
}

#ifdef CONFIG_DMA_HELP
void dma_dump(enum dma_channel stream)
{
	stm32_dma_stream_t *dma_stream = dma_get_channel(stream);

	CPRINTF("scr=%x, sndtr=%x, spar=%x, sm0ar=%x, sfcr=%x\n",
		dma_stream->scr, dma_stream->sndtr, dma_stream->spar,
		dma_stream->sm0ar, dma_stream->sfcr);
	CPRINTF("stream %d, isr=%x, ifcr=%x\n",
		stream,
		STM32_DMA_GET_ISR(stream),
		STM32_DMA_GET_IFCR(stream));
}

void dma_check(enum dma_channel stream, char *buf)
{
	stm32_dma_stream_t *dma_stream = dma_get_channel(stream);
	int count;
	int i;

	count = dma_stream->sndtr;
	CPRINTF("c=%d\n", count);
	udelay(100 * MSEC);
	CPRINTF("c=%d\n", dma_stream->sndtr);
	for (i = 0; i < count; i++)
		CPRINTF("%02x ", buf[i]);
	udelay(100 * MSEC);
	CPRINTF("c=%d\n", dma_stream->sndtr);
	for (i = 0; i < count; i++)
		CPRINTF("%02x ", buf[i]);
}

/* Run a check of memory-to-memory DMA */
void dma_test(enum dma_channel stream)
{
	stm32_dma_stream_t *dma_stream = dma_get_channel(stream);
	uint32_t ctrl;
	char periph[32], memory[32];
	unsigned count = sizeof(periph);
	int i;

	memset(memory, '\0', sizeof(memory));
	for (i = 0; i < count; i++)
		periph[i] = 10 + i;

	dma_clear_isr(stream);
	/* Following the order in Doc ID 15965 Rev 5 p194 */
	dma_stream->spar = (uint32_t)periph;
	dma_stream->sm0ar = (uint32_t)memory;
	dma_stream->sndtr = count;
	dma_stream->sfcr  &= ~STM32_DMA_SFCR_DMDIS;
	ctrl = STM32_DMA_CCR_PL_MEDIUM;
	dma_stream->scr = ctrl;

	ctrl |= STM32_DMA_CCR_MINC;
	ctrl |= STM32_DMA_CCR_DIR_M2M;
	ctrl |= STM32_DMA_CCR_PINC;

	dma_stream->scr = ctrl;
	dma_dump(stream);
	dma_stream->scr = ctrl | STM32_DMA_CCR_EN;

	for (i = 0; i < count; i++)
		CPRINTF("%d/%d ", periph[i], memory[i]);
	CPRINTF("\ncount=%d\n", dma_stream->sndtr);
	dma_dump(stream);
}
#endif /* CONFIG_DMA_HELP */

void dma_init(void)
{
	STM32_RCC_AHB1ENR |= STM32_RCC_HB1_DMA1 | STM32_RCC_HB1_DMA2;
}

int dma_wait(enum dma_channel stream)
{
	timestamp_t deadline;

	deadline.val = get_time().val + DMA_TRANSFER_TIMEOUT_US;
	while ((STM32_DMA_GET_ISR(stream) & STM32_DMA_TCIF) == 0) {
		if (deadline.val <= get_time().val)
			return EC_ERROR_TIMEOUT;

		udelay(DMA_POLLING_INTERVAL_US);
	}
	return EC_SUCCESS;
}

static inline void _dma_wake_callback(void *cb_data)
{
	task_id_t id = (task_id_t)(int)cb_data;

	if (id != TASK_ID_INVALID)
		task_set_event(id, TASK_EVENT_DMA_TC, 0);
}

void dma_enable_tc_interrupt(enum dma_channel stream)
{
	dma_enable_tc_interrupt_callback(stream, _dma_wake_callback,
					 (void *)(int)task_get_current());
}

void dma_enable_tc_interrupt_callback(enum dma_channel stream,
				      void (*callback)(void *),
				      void *callback_data)
{
	stm32_dma_stream_t *dma_stream = dma_get_channel(stream);

	dma_irq[stream].cb = callback;
	dma_irq[stream].cb_data = callback_data;

	dma_stream->scr |= STM32_DMA_CCR_TCIE;
	task_enable_irq(dma_get_irq(stream));
}

void dma_disable_tc_interrupt(enum dma_channel stream)
{
	stm32_dma_stream_t *dma_stream = dma_get_channel(stream);

	dma_stream->scr &= ~STM32_DMA_CCR_TCIE;
	task_disable_irq(dma_get_irq(stream));

	dma_irq[stream].cb = NULL;
	dma_irq[stream].cb_data = NULL;
}

void dma_clear_isr(enum dma_channel stream)
{
	STM32_DMA_SET_IFCR(stream, STM32_DMA_ALL);
}

#ifdef CONFIG_DMA_DEFAULT_HANDLERS
#define STM32_DMA_IDX(dma, x)   CONCAT4(STM32_DMA, dma, _STREAM, x)
#define STM32_DMA_FCT(dma, x)   CONCAT4(dma_, dma, _event_interrupt_stream_, x)
#define DECLARE_DMA_IRQ(dma, x) \
	void STM32_DMA_FCT(dma, x)(void) \
	{ \
		dma_clear_isr(STM32_DMA_IDX(dma, x)); \
		if (dma_irq[STM32_DMA_IDX(dma, x)].cb != NULL) \
			(*dma_irq[STM32_DMA_IDX(dma, x)].cb) \
				(dma_irq[STM32_DMA_IDX(dma, x)].cb_data); \
	} \
	DECLARE_IRQ(CONCAT4(STM32_IRQ_DMA, dma, _STREAM, x), \
		    STM32_DMA_FCT(dma, x), 1);

DECLARE_DMA_IRQ(1, 0);
DECLARE_DMA_IRQ(1, 1);
DECLARE_DMA_IRQ(1, 2);
DECLARE_DMA_IRQ(1, 3);
DECLARE_DMA_IRQ(1, 4);
DECLARE_DMA_IRQ(1, 5);
DECLARE_DMA_IRQ(1, 6);
DECLARE_DMA_IRQ(1, 7);
DECLARE_DMA_IRQ(2, 0);
DECLARE_DMA_IRQ(2, 1);
DECLARE_DMA_IRQ(2, 2);
DECLARE_DMA_IRQ(2, 3);
DECLARE_DMA_IRQ(2, 4);
DECLARE_DMA_IRQ(2, 5);
DECLARE_DMA_IRQ(2, 6);
DECLARE_DMA_IRQ(2, 7);

#endif /* CONFIG_DMA_DEFAULT_HANDLERS */

