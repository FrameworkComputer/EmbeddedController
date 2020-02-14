/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DMA module for ISH */

#include "common.h"
#include "console.h"
#include "registers.h"
#include "ish_dma.h"
#include "util.h"

static int dma_init_called; /* If ish_dma_init is called */

static int dma_poll(uint32_t addr, uint32_t expected, uint32_t mask)
{
	int retval = -1;
	uint32_t counter = 0;

	/*
	 * The timeout is approximately 2.2 seconds according to
	 * value of UINT32_MAX, 120MHZ ISH clock frequency and
	 * instruction count which is around 4.
	 */
	while (counter < (UINT32_MAX / 64)) {
		/* test condition */
		if ((REG32(addr) & mask) == expected) {
			retval = DMA_RC_OK;
			break;
		}
		counter++;
	}

	return retval;
}

void ish_dma_ocp_timeout_disable(void)
{
	if (!IS_ENABLED(CONFIG_ISH_NEW_PM)) {
		uint32_t ctrl = OCP_AGENT_CONTROL;

		OCP_AGENT_CONTROL = ctrl & OCP_RESPONSE_TO_DISABLE;
	}
}

static inline uint32_t interrupt_lock(void)
{
	uint32_t eflags = 0;
	__asm__ volatile("pushfl;" /* save eflag value */
			 "popl  %0;"
			 "cli;"
			 : "=r"(eflags)); /* shut off interrupts */
	return eflags;
}

static inline void interrupt_unlock(uint32_t eflags)
{
	__asm__ volatile("pushl  %0;" /* restore elfag values */
			 "popfl;"
			 :
			 : "r"(eflags));
}

void dma_configure_psize(void)
{
	/* Give chan0 512 bytes for high performance, and chan1 128 bytes. */
	DMA_PSIZE_01 = DMA_PSIZE_UPDATE |
		       (DMA_PSIZE_CHAN1_SIZE << DMA_PSIZE_CHAN1_OFFSET) |
		       (DMA_PSIZE_CHAN0_SIZE << DMA_PSIZE_CHAN0_OFFSET);
}

void ish_dma_init(void)
{
	uint32_t uma_msb;

	ish_dma_ocp_timeout_disable();

	/* configure DMA partition size */
	dma_configure_psize();

	/* set DRAM address 32 MSB for DMA transactions on UMA */
	uma_msb = IPC_UMA_RANGE_LOWER_1;
	ish_dma_set_msb(PAGING_CHAN, uma_msb, uma_msb);

	dma_init_called = 1;
}

int ish_dma_copy(uint32_t chan, uint32_t dst, uint32_t src, uint32_t length,
		 enum dma_mode mode)
{
	uint32_t chan_reg = DMA_REG_BASE + (DMA_CH_REGS_SIZE * chan);
	int rc = DMA_RC_OK;
	uint32_t eflags;
	uint32_t chunk;

	__asm__ volatile("\twbinvd\n"); /* Flush cache before dma start */

	/* Bringup VNN power rail for accessing SoC fabric */
	PMU_VNN_REQ = (1 << VNN_ID_DMA(chan));
	while (!(PMU_VNN_REQ_ACK & PMU_VNN_REQ_ACK_STATUS))
		continue;

	/*
	 * shut off interrupts to assure no simultanious
	 * access to DMA registers
	 */
	eflags = interrupt_lock();

	MISC_CHID_CFG_REG = chan; /* Set channel to configure */

	mode |= NON_SNOOP;
	MISC_DMA_CTL_REG(chan) = mode; /* Set transfer direction */

	DMA_CFG_REG = DMA_ENABLE;  /* Enable DMA module */
	DMA_LLP(chan_reg) = 0;     /* Linked lists are not used */
	DMA_CTL_LOW(chan_reg) =
		0 /* Set transfer parameters */ |
		(DMA_CTL_TT_FC_M2M_DMAC << DMA_CTL_TT_FC_SHIFT) |
		(DMA_CTL_ADDR_INC << DMA_CTL_SINC_SHIFT) |
		(DMA_CTL_ADDR_INC << DMA_CTL_DINC_SHIFT) |
		(SRC_TR_WIDTH << DMA_CTL_SRC_TR_WIDTH_SHIFT) |
		(DEST_TR_WIDTH << DMA_CTL_DST_TR_WIDTH_SHIFT) |
		(SRC_BURST_SIZE << DMA_CTL_SRC_MSIZE_SHIFT) |
		(DEST_BURST_SIZE << DMA_CTL_DEST_MSIZE_SHIFT);

	interrupt_unlock(eflags);
	while (length) {
		chunk = (length > DMA_MAX_BLOCK_SIZE) ? DMA_MAX_BLOCK_SIZE
						      : length;

		if (rc != DMA_RC_OK)
			break;

		eflags = interrupt_lock();
		MISC_CHID_CFG_REG = chan; /* Set channel to configure */
		DMA_CTL_HIGH(chan_reg) =
			chunk;		 /* Set number of bytes to transfer */
		DMA_DAR(chan_reg) = dst; /* Destination address */
		DMA_SAR(chan_reg) = src; /* Source address */
		DMA_EN_REG = DMA_CH_EN_BIT(chan) |
			     DMA_CH_EN_WE_BIT(chan); /* Enable the channel */
		interrupt_unlock(eflags);

		rc = ish_wait_for_dma_done(
			chan); /* Wait for trans completion */

		dst += chunk;
		src += chunk;
		length -= chunk;
	}

	/* Mark the DMA VNN power rail as no longer needed */
	PMU_VNN_REQ = (1 << VNN_ID_DMA(chan));
	return rc;
}

void ish_dma_disable(void)
{
	uint32_t channel;
	uint32_t counter;

	/* Disable DMA on per-channel basis. */
	for (channel = 0; channel <= DMA_MAX_CHANNEL; channel++) {
		MISC_CHID_CFG_REG = channel;
		if (DMA_EN_REG & DMA_CH_EN_BIT(channel)) {
			/* Write 0 to channel enable bit ... */
			DMA_EN_REG = DMA_CH_EN_WE_BIT(channel);

			/* Wait till it shuts up. */
			counter = 0;
			while ((DMA_EN_REG & DMA_CH_EN_BIT(channel)) &&
			       counter < (UINT32_MAX / 64))
				counter++;
		}
	}
	DMA_CLR_ERR_REG = 0xFFFFFFFF;
	DMA_CLR_BLOCK_REG = 0xFFFFFFFF;

	DMA_CFG_REG = 0; /* Disable DMA module */
}

int ish_wait_for_dma_done(uint32_t ch)
{
	return dma_poll(DMA_EN_REG_ADDR, 0, DMA_CH_EN_BIT(ch));
}

void ish_dma_set_msb(uint32_t chan, uint32_t dst_msb, uint32_t src_msb)
{
	uint32_t eflags = interrupt_lock();
	MISC_CHID_CFG_REG = chan; /* Set channel to configure */
	MISC_SRC_FILLIN_DMA(chan) = src_msb;
	MISC_DST_FILLIN_DMA(chan) = dst_msb;
	interrupt_unlock(eflags);
}
