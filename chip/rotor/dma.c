/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * DMA driver for Rotor MCU
 */

#include "common.h"
#include "dma.h"
#include "registers.h"

rotor_mcu_dma_chan_t *dma_get_channel(enum dma_channel channel)
{
	rotor_mcu_dma_regs_t *dma = ROTOR_MCU_DMA_REGS;

	return &dma->chan[channel];
}

void dma_prepare_tx(const struct dma_option *option, unsigned count,
		    const void *memory)
{
	rotor_mcu_dma_chan_t *chan = dma_get_channel(option->channel);

	/* TODO(aaboagye): Actually set up a transaction. */
}
