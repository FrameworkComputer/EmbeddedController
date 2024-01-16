/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "dma.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "tfdp_chip.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_DMA, outstr)
#define CPRINTS(format, args...) cprints(CC_DMA, format, ##args)

dma_chan_t *dma_get_channel(enum dma_channel channel)
{
	dma_chan_t *pd = NULL;

	if (channel < MCHP_DMAC_COUNT) {
		pd = (dma_chan_t *)(MCHP_DMA_BASE + MCHP_DMA_CH_OFS +
				    (channel << MCHP_DMA_CH_OFS_BITPOS));
	}

	return pd;
}

void dma_disable(enum dma_channel channel)
{
	if (channel < MCHP_DMAC_COUNT) {
		if (MCHP_DMA_CH_CTRL(channel) & MCHP_DMA_RUN)
			MCHP_DMA_CH_CTRL(channel) &= ~(MCHP_DMA_RUN);

		if (MCHP_DMA_CH_ACT(channel) & MCHP_DMA_ACT_EN)
			MCHP_DMA_CH_ACT(channel) = 0;
	}
}

void dma_disable_all(void)
{
	uint16_t ch;
	__maybe_unused uint32_t unused = 0;

	for (ch = 0; ch < MCHP_DMAC_COUNT; ch++) {
		/* Abort any current transfer. */
		MCHP_DMA_CH_CTRL(ch) |= MCHP_DMA_ABORT;
		/* Disable the channel. */
		MCHP_DMA_CH_CTRL(ch) &= ~(MCHP_DMA_RUN);
		MCHP_DMA_CH_ACT(ch) = 0;
	}

	/* Soft-reset the block. */
	MCHP_DMA_MAIN_CTRL = MCHP_DMA_MAIN_CTRL_SRST;
	unused += MCHP_DMA_MAIN_CTRL;
	MCHP_DMA_MAIN_CTRL = MCHP_DMA_MAIN_CTRL_ACT;
}

/**
 * Prepare a channel for use and start it
 *
 * @param chan	Channel to read
 * @param count	Number of bytes to transfer
 * @param periph Pointer to peripheral data register
 * @param memory Pointer to memory address for receive/transmit
 * @param flags	DMA flags for the control register, normally:
 *		MCHP_DMA_INC_MEM | MCHP_DMA_TO_DEV for tx
 *		MCHP_DMA_INC_MEM for rx
 *		Plus transfer unit length(1, 2, or 4) in bits[22:20]
 * @note MCHP DMA does not require address aliasing. Because count
 * is the number of bytes to transfer memory start - memory end = count.
 */
static void prepare_channel(enum dma_channel ch, unsigned int count,
			    void *periph, void *memory, unsigned int flags)
{
	if (ch < MCHP_DMAC_COUNT) {
		MCHP_DMA_CH_CTRL(ch) = 0;
		MCHP_DMA_CH_MEM_START(ch) = (uint32_t)memory;
		MCHP_DMA_CH_MEM_END(ch) = (uint32_t)memory + count;

		MCHP_DMA_CH_DEV_ADDR(ch) = (uint32_t)periph;

		MCHP_DMA_CH_CTRL(ch) = flags;
		MCHP_DMA_CH_ACT(ch) = MCHP_DMA_ACT_EN;
	}
}

void dma_go(dma_chan_t *chan)
{
	/* Flush data in write buffer so that DMA can get the
	 *  latest data.
	 */
	asm volatile("dsb;");

	if (chan != NULL)
		chan->ctrl |= MCHP_DMA_RUN;
}

void dma_go_chan(enum dma_channel ch)
{
	asm volatile("dsb;");
	if (ch < MCHP_DMAC_COUNT)
		MCHP_DMA_CH_CTRL(ch) |= MCHP_DMA_RUN;
}

void dma_prepare_tx(const struct dma_option *option, unsigned int count,
		    const void *memory)
{
	if (option != NULL)
		/*
		 * Cast away const for memory pointer; this is ok because
		 * we know we're preparing the channel for transmit.
		 */
		prepare_channel(
			option->channel, count, option->periph, (void *)memory,
			MCHP_DMA_INC_MEM | MCHP_DMA_TO_DEV |
				MCHP_DMA_DEV(option->channel) | option->flags);
}

void dma_xfr_prepare_tx(const struct dma_option *option, uint32_t count,
			const void *memory, uint32_t dma_xfr_units)
{
	uint32_t nflags;

	if (option != NULL) {
		nflags = option->flags & ~(MCHP_DMA_XFER_SIZE_MASK);
		nflags |= MCHP_DMA_XFER_SIZE(dma_xfr_units & 0x07);
		/*
		 * Cast away const for memory pointer; this is ok because
		 * we know we're preparing the channel for transmit.
		 */
		prepare_channel(option->channel, count, option->periph,
				(void *)memory,
				MCHP_DMA_INC_MEM | MCHP_DMA_TO_DEV |
					MCHP_DMA_DEV(option->channel) | nflags);
	}
}

void dma_start_rx(const struct dma_option *option, unsigned int count,
		  void *memory)
{
	if (option != NULL) {
		prepare_channel(option->channel, count, option->periph, memory,
				MCHP_DMA_INC_MEM |
					MCHP_DMA_DEV(option->channel) |
					option->flags);
		dma_go_chan(option->channel);
	}
}

/*
 * Configure and start DMA channel for read from device and write to
 * memory. Allow caller to override DMA transfer unit length.
 */
void dma_xfr_start_rx(const struct dma_option *option, uint32_t dma_xfr_ulen,
		      uint32_t count, void *memory)
{
	uint32_t ch, ctrl;

	if (option != NULL) {
		ch = option->channel;
		if (ch < MCHP_DMAC_COUNT) {
			MCHP_DMA_CH_CTRL(ch) = 0;
			MCHP_DMA_CH_MEM_START(ch) = (uint32_t)memory;
			MCHP_DMA_CH_MEM_END(ch) = (uint32_t)memory + count;

			MCHP_DMA_CH_DEV_ADDR(ch) = (uint32_t)option->periph;

			ctrl = option->flags & ~(MCHP_DMA_XFER_SIZE_MASK);
			ctrl |= MCHP_DMA_INC_MEM;
			ctrl |= MCHP_DMA_XFER_SIZE(dma_xfr_ulen);
			ctrl |= MCHP_DMA_DEV(option->channel);
			MCHP_DMA_CH_CTRL(ch) = ctrl;
			MCHP_DMA_CH_ACT(ch) = MCHP_DMA_ACT_EN;
		}

		dma_go_chan(option->channel);
	}
}

/*
 * Return the number of bytes transferred.
 * The number of bytes transferred can be easily determined
 * from the difference in DMA memory start address register
 * and memory end address register. No need to look at DMA
 * transfer size field because the hardware increments memory
 * start address by unit size on each unit transferred.
 * Why is a signed integer being used for a count value?
 */
int dma_bytes_done(dma_chan_t *chan, int orig_count)
{
	int bcnt;

	if (chan == NULL)
		return 0;

	bcnt = (int)chan->mem_end;
	bcnt -= (int)chan->mem_start;
	bcnt = orig_count - bcnt;

	return bcnt;
}

bool dma_is_enabled(dma_chan_t *chan)
{
	return (chan->ctrl & MCHP_DMA_RUN);
}

int dma_bytes_done_chan(enum dma_channel ch, uint32_t orig_count)
{
	uint32_t cnt;

	cnt = 0;
	if (ch < MCHP_DMAC_COUNT)
		if (MCHP_DMA_CH_CTRL(ch) & MCHP_DMA_RUN)
			cnt = (uint32_t)orig_count -
			      (MCHP_DMA_CH_MEM_END(ch) -
			       MCHP_DMA_CH_MEM_START(ch));

	return (int)cnt;
}

/*
 * Initialize DMA block.
 * Clear PCR DMA sleep enable.
 * Soft-Reset block should clear after one clock but read-back to
 * be safe.
 * Set block activate bit after reset.
 */
void dma_init(void)
{
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_DMA);
	MCHP_DMA_MAIN_CTRL = MCHP_DMA_MAIN_CTRL_SRST;
	MCHP_DMA_MAIN_CTRL;
	MCHP_DMA_MAIN_CTRL = MCHP_DMA_MAIN_CTRL_ACT;
}

int dma_wait(enum dma_channel channel)
{
	timestamp_t deadline;

	if (channel < MCHP_DMAC_COUNT) {
		if (MCHP_DMA_CH_ACT(channel) == 0)
			return EC_SUCCESS;

		deadline.val = get_time().val + DMA_TRANSFER_TIMEOUT_US;

		while (!(MCHP_DMA_CH_ISTS(channel) & MCHP_DMA_STS_DONE)) {
			if (deadline.val <= get_time().val)
				return EC_ERROR_TIMEOUT;

			udelay(DMA_POLLING_INTERVAL_US);
		}
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;
}

/*
 * Clear all interrupt status in specified DMA channel
 */
void dma_clear_isr(enum dma_channel channel)
{
	if (channel < MCHP_DMAC_COUNT)
		MCHP_DMA_CH_ISTS(channel) = 0x0f;
}

void dma_cfg_buffers(enum dma_channel ch, const void *membuf, uint32_t nb,
		     const void *pdev)
{
	if (ch < MCHP_DMAC_COUNT) {
		MCHP_DMA_CH_MEM_START(ch) = (uint32_t)membuf;
		MCHP_DMA_CH_MEM_END(ch) = (uint32_t)membuf + nb;
		MCHP_DMA_CH_DEV_ADDR(ch) = (uint32_t)pdev;
	}
}

/*
 * ch = zero based DMA channel number
 * unit_len = DMA unit size 1, 2 or 4 bytes
 * flags
 *   b[0] = direction, 0=device_to_memory, 1=memory_to_device
 *   b[1] = 1 increment memory address
 *   b[2] = 1 increment device address
 *   b[3] = disable HW flow control
 */
void dma_cfg_xfr(enum dma_channel ch, uint8_t unit_len, uint8_t dev_id,
		 uint8_t flags)
{
	uint32_t ctrl;

	if (ch < MCHP_DMAC_COUNT) {
		ctrl = MCHP_DMA_XFER_SIZE(unit_len & 0x07);
		ctrl += MCHP_DMA_DEV(dev_id & MCHP_DMA_DEV_MASK0);
		if (flags & 0x01)
			ctrl |= MCHP_DMA_TO_DEV;
		if (flags & 0x02)
			ctrl |= MCHP_DMA_INC_MEM;
		if (flags & 0x04)
			ctrl |= MCHP_DMA_INC_DEV;
		if (flags & 0x08)
			ctrl |= MCHP_DMA_DIS_HW_FLOW;
		MCHP_DMA_CH_CTRL(ch) = ctrl;
	}
}

void dma_clr_chan(enum dma_channel ch)
{
	if (ch < MCHP_DMAC_COUNT) {
		MCHP_DMA_CH_ACT(ch) = 0;
		MCHP_DMA_CH_CTRL(ch) = 0;
		MCHP_DMA_CH_IEN(ch) = 0;
		MCHP_DMA_CH_ISTS(ch) = 0xff;
		MCHP_DMA_CH_FSM_RO(ch) = MCHP_DMA_CH_ISTS(ch);
		MCHP_DMA_CH_ACT(ch) = 1;
	}
}

void dma_run(enum dma_channel ch)
{
	if (ch < MCHP_DMAC_COUNT) {
		if (MCHP_DMA_CH_CTRL(ch) & MCHP_DMA_DIS_HW_FLOW)
			MCHP_DMA_CH_CTRL(ch) |= MCHP_DMA_SW_GO;
		else
			MCHP_DMA_CH_CTRL(ch) |= MCHP_DMA_RUN;
	}
}

/*
 * Check if DMA channel is done or stopped on error
 * Returns 0 not done or stopped on error
 * Returns non-zero if done or stopped.
 * Caller should check bit pattern for specific bit,
 * done, flow control error, and bus error.
 */
uint32_t dma_is_done_chan(enum dma_channel ch)
{
	if (ch < MCHP_DMAC_COUNT)
		return (uint32_t)(MCHP_DMA_CH_ISTS(ch) & 0x07);

	return 0;
}

/*
 * Use DMA Channel 0 CRC32 ALU to compute CRC32 of data.
 * Hardware implements IEEE 802.3 CRC32.
 * IEEE 802.3 CRC32 initial value = 0xffffffff.
 * Data must be aligned >= 4-bytes and number of bytes must
 * be a multiple of 4.
 */
int dma_crc32_start(const uint8_t *mstart, const uint32_t nbytes, int ien)
{
	if ((mstart == NULL) || (nbytes == 0))
		return EC_ERROR_INVAL;

	if ((((uint32_t)mstart | nbytes) & 0x03) != 0)
		return EC_ERROR_INVAL;

	MCHP_DMA_CH_ACT(0) = 0;
	MCHP_DMA_CH_CTRL(0) = 0;
	MCHP_DMA_CH_IEN(0) = 0;
	MCHP_DMA_CH_ISTS(0) = 0xff;
	MCHP_DMA_CH0_CRC32_EN = 1;
	MCHP_DMA_CH0_CRC32_DATA = 0xfffffffful;
	/* program device address to point to read-only register */
	MCHP_DMA_CH_DEV_ADDR(0) = (uint32_t)(MCHP_DMA_CH_BASE + 0x1c);
	MCHP_DMA_CH_MEM_START(0) = (uint32_t)mstart;
	MCHP_DMA_CH_MEM_END(0) = (uint32_t)mstart + nbytes;
	if (ien != 0)
		MCHP_DMA_CH_IEN(0) = 0x07;
	MCHP_DMA_CH_ACT(0) = 1;
	MCHP_DMA_CH_CTRL(0) = MCHP_DMA_TO_DEV + MCHP_DMA_INC_MEM +
			      MCHP_DMA_DIS_HW_FLOW + MCHP_DMA_XFER_SIZE(4);
	MCHP_DMA_CH_CTRL(0) |= MCHP_DMA_SW_GO;
	return EC_SUCCESS;
}
