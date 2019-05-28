/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ISH_DMA_H
#define __CROS_EC_ISH_DMA_H

/* DMA return codes */
#define DMA_RC_OK 0 /* Success */
#define DMA_RC_TO 1 /* Time out */
#define DMA_RC_HW 2 /* HW error (OCP) */

/* DMA channels */
#define PAGING_CHAN 0
#define KERNEL_CHAN 1

#define DST_IS_DRAM BIT(0)
#define SRC_IS_DRAM BIT(1)
#define NON_SNOOP BIT(2)

/* ISH5 and on */
#define RS0 0x0
#define RS3 0x3
#define RS_SRC_OFFSET 3
#define RS_DST_OFFSET 5

#define PAGE_SIZE 4096

/**
 * SRAM: ISH local static ram
 * UMA: Protected system DRAM region dedicated for ISH
 * HOST_DRAM: OS owned buffer in system DRAM
 */
enum dma_mode {
	SRAM_TO_SRAM = 0,
	SRAM_TO_UMA = DST_IS_DRAM | (RS3 << RS_DST_OFFSET),
	UMA_TO_SRAM = SRC_IS_DRAM | (RS3 << RS_SRC_OFFSET),
	HOST_DRAM_TO_SRAM = SRC_IS_DRAM | (RS0 << RS_SRC_OFFSET),
	SRAM_TO_HOST_DRAM = DST_IS_DRAM | (RS0 << RS_DST_OFFSET)
};

/* Disable DMA engine */
void ish_dma_disable(void);
/* Initialize  DMA engine */
void ish_dma_init(void);

/**
 * Main DMA transfer function
 *
 * @param chan          DMA channel
 * @param dst           Destination address
 * @param src           Source address
 * @param length        Transfer size
 * @param mode          Transfer mode
 * @return DMA_RC_OK, or non-zero if error.
 */
int ish_dma_copy(uint32_t chan, uint32_t dst, uint32_t src, uint32_t length,
		 enum dma_mode mode);
/**
 * Set upper 32 bits address for DRAM
 *
 * @param chan          DMA channel
 * @param dst_msb       Destination DRAM upper 32 bits address
 * @param src_msb       Source DRAM upper 32 bits address
 */
void ish_dma_set_msb(uint32_t chan, uint32_t dst_msb, uint32_t src_msb);

/**
 * Wait for DMA transfer finish
 *
 * @param chan          DMA channel
 * @return DMA_RC_OK, or non-zero if error.
 */
int ish_wait_for_dma_done(uint32_t ch);

/* Disable OCP (Open Core Protocol) fabric time out */
void ish_dma_ocp_timeout_disable(void);
#endif
