/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "audio_codec.h"
#include "hooks.h"
#include "memmap.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/* VIF FIFO irq is triggered above this level */
#define WOV_TRIGGER_LEVEL 160

int audio_codec_wov_enable_notifier(void)
{
	SCP_VIF_FIFO_DATA_THRE = WOV_TRIGGER_LEVEL + 1;
	SCP_VIF_FIFO_EN |= VIF_FIFO_IRQ_EN;

	task_enable_irq(SCP_IRQ_MAD_FIFO);

	return EC_SUCCESS;
}

int audio_codec_wov_disable_notifier(void)
{
	SCP_VIF_FIFO_EN &= ~VIF_FIFO_IRQ_EN;

	task_disable_irq(SCP_IRQ_MAD_FIFO);

	return EC_SUCCESS;
}

int audio_codec_wov_enable(void)
{
	SCP_VIF_FIFO_EN = 0;

	SCP_RXIF_CFG0 = (RXIF_CFG0_RESET_VAL & ~RXIF_RGDL2_MASK) |
			RXIF_RGDL2_DMIC_16K;
	SCP_RXIF_CFG1 = RXIF_CFG1_RESET_VAL;

	SCP_VIF_FIFO_EN |= VIF_FIFO_RSTN;

	return EC_SUCCESS;
}

int audio_codec_wov_disable(void)
{
	SCP_VIF_FIFO_EN = 0;

	return EC_SUCCESS;
}

static size_t wov_fifo_level(void)
{
	uint32_t fifo_status = SCP_VIF_FIFO_STATUS;

	if (!(fifo_status & VIF_FIFO_VALID))
		return 0;

	if (fifo_status & VIF_FIFO_FULL)
		return VIF_FIFO_MAX;

	return VIF_FIFO_LEVEL(fifo_status);
}

int32_t audio_codec_wov_read(void *buf, uint32_t count)
{
	int16_t *out = buf;
	uint8_t gain = 1;

	if (IS_ENABLED(CONFIG_AUDIO_CODEC_DMIC_SOFTWARE_GAIN))
		audio_codec_dmic_get_gain_idx(0, &gain);

	count >>= 1;

	while (count-- && wov_fifo_level()) {
		if (IS_ENABLED(CONFIG_AUDIO_CODEC_DMIC_SOFTWARE_GAIN))
			*out++ = audio_codec_s16_scale_and_clip(
					SCP_VIF_FIFO_DATA, gain);
		else
			*out++ = SCP_VIF_FIFO_DATA;
	}

	return (void *)out - buf;
}

static void wov_fifo_interrupt_handler(void)
{
#ifdef HAS_TASK_WOV
	task_wake(TASK_ID_WOV);
#endif

	audio_codec_wov_disable_notifier();

	/* Read to clear */
	SCP_VIF_FIFO_IRQ_STATUS;
}
DECLARE_IRQ(SCP_IRQ_MAD_FIFO, wov_fifo_interrupt_handler, 2);

int audio_codec_memmap_ap_to_ec(uintptr_t ap_addr, uintptr_t *ec_addr)
{
	return memmap_ap_to_scp(ap_addr, ec_addr);
}
