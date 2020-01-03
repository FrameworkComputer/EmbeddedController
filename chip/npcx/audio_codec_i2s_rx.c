/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "audio_codec.h"
#include "ec_commands.h"
#include "wov_chip.h"

#define CPRINTS(format, args...) cprints(CC_AUDIO_CODEC, format, ## args)

int audio_codec_i2s_rx_enable(void)
{
	/*
	 * The mic source and sample rate don't need to be set each time
	 * an i2s stream is started, but the audio codec does not
	 * contain a method to select these as they must be the values
	 * set below for proper i2s operation. Since the default values
	 * set in wov.c are different than what's required, they are set
	 * each time an i2s stream is started.
	 */
	wov_set_mic_source(WOV_SRC_STEREO);

	/* Mode must be WOV_MODE_OFF to change sample rate */
	wov_set_mode(WOV_MODE_OFF);
	wov_set_sample_rate(48000);

	return wov_set_mode(WOV_MODE_I2S);
}

int audio_codec_i2s_rx_disable(void)
{
	return wov_set_mode(WOV_MODE_OFF);
}

int audio_codec_i2s_rx_set_sample_depth(uint8_t depth)
{
	int bits_num;

	if (depth == EC_CODEC_I2S_RX_SAMPLE_DEPTH_24)
		bits_num = 24;
	else
		bits_num = 16;

	/* Sample depth can only be changed when mode is WOV_MODE_OFF */
	wov_set_mode(WOV_MODE_OFF);
	return wov_set_sample_depth(bits_num);
}

int audio_codec_i2s_rx_set_daifmt(uint8_t daifmt)
{
	enum wov_dai_format fmt = WOV_DAI_FMT_I2S;

	switch (daifmt) {
	case EC_CODEC_I2S_RX_DAIFMT_I2S:
		fmt = WOV_DAI_FMT_I2S;
		break;
	case EC_CODEC_I2S_RX_DAIFMT_RIGHT_J:
		fmt = WOV_DAI_FMT_RIGHT_J;
		break;
	case EC_CODEC_I2S_RX_DAIFMT_LEFT_J:
		fmt = WOV_DAI_FMT_LEFT_J;
		break;
	}

	/* To change mode setting it must be set to WOV_MODE_OFF */
	wov_set_mode(WOV_MODE_OFF);
	wov_set_i2s_fmt(fmt);

	return EC_SUCCESS;
}

int audio_codec_i2s_rx_set_bclk(uint32_t bclk)
{
	/* To change bclk setting it must be set to WOV_MODE_OFF */
	wov_set_mode(WOV_MODE_OFF);
	wov_set_i2s_bclk(bclk);
	return EC_SUCCESS;
}
