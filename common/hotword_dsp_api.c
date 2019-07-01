/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "audio_codec.h"
#include "hotword_dsp_api.h"

const int kGoogleHotwordRequiredDataAlignment = 4;

int GoogleHotwordDspInit(void *hotword_memmap)
{
	return 1;
}

int GoogleHotwordDspProcess(const void *samples, int num_samples,
			    int *preamble_length_ms)
{
	return 0;
}

void GoogleHotwordDspReset(void)
{
}

int GoogleHotwordDspGetMaximumAudioPreambleMs(void)
{
	return 0;
}

int GoogleHotwordVersion(void)
{
	return 0;
}
