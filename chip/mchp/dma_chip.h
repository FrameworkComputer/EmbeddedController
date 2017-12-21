/* Copyright 2017 The Chromium OS Authors. All rights reserved
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MCHP MEC DMA controller chip level API
 */
/** @file dma_chip.h
 *MCHP MEC Direct Memory Access block
 */
/** @defgroup MEC dma
 */

#ifndef _DMA_CHIP_H
#define _DMA_CHIP_H

#include <stdint.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

/* Place any C interfaces here */

void dma_xfr_start_rx(const struct dma_option *option,
		uint32_t dma_xfr_ulen,
		uint32_t count, void *memory);

void dma_xfr_prepare_tx(const struct dma_option *option, uint32_t count,
		const void *memory, uint32_t dma_xfr_units);

void dma_clr_chan(enum dma_channel ch);

void dma_cfg_buffers(enum dma_channel ch, const void *membuf,
			uint32_t nb, const void *pdev);

/*
 * ch = zero based DMA channel number
 * unit_len = DMA unit size 1, 2 or 4 bytes
 * flags
 *   b[0] = direction, 0=device_to_memory, 1=memory_to_device
 *   b[1] = 1 increment memory address
 *   b[2] = 1 increment device address
 *   b[3] = disable HW flow control
 */
#define DMA_FLAG_D2M		0
#define DMA_FLAG_M2D		1
#define DMA_FLAG_INCR_MEM	2
#define DMA_FLAG_INCR_DEV	4
#define DMA_FLAG_SW_FLOW	8
void dma_cfg_xfr(enum dma_channel ch, uint8_t unit_len,
			uint8_t dev_id, uint8_t flags);

void dma_run(enum dma_channel ch);

uint32_t dma_is_done_chan(enum dma_channel ch);

int dma_crc32_start(const uint8_t *mstart, const uint32_t nbytes, int ien);

#ifdef __cplusplus
}
#endif

#endif /* #ifndef _DMA_CHIP_H */
/**   @}
 */

