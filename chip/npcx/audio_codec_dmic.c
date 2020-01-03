/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "audio_codec.h"
#include "ec_commands.h"
#include "wov_chip.h"

int audio_codec_dmic_get_max_gain(uint8_t *gain)
{
	*gain = 31;
	return EC_SUCCESS;
}

int audio_codec_dmic_set_gain_idx(uint8_t channel, uint8_t gain)
{
	int left_gain, right_gain;

	wov_get_gain(&left_gain, &right_gain);

	switch (channel) {
	case EC_CODEC_DMIC_CHANNEL_0:
		left_gain = gain;
		break;
	case EC_CODEC_DMIC_CHANNEL_1:
		right_gain = gain;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	wov_set_gain(left_gain, right_gain);
	return EC_SUCCESS;
}

int audio_codec_dmic_get_gain_idx(uint8_t channel, uint8_t *gain)
{
	int left_gain, right_gain;

	wov_get_gain(&left_gain, &right_gain);

	switch (channel) {
	case EC_CODEC_DMIC_CHANNEL_0:
		*gain = left_gain;
		break;
	case EC_CODEC_DMIC_CHANNEL_1:
		*gain = right_gain;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}
