/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SPEECH_MICRO_API_HOTWORD_DSP_API_H_
#define SPEECH_MICRO_API_HOTWORD_DSP_API_H_

/*
 * This API creates a global singleton behind the scenes. It's the caller's
 * responsibility to store the contents of the hotword_memmap model file into
 * aligned memory and pass its pointer to this library. Note that no additional
 * memory is allocated and hotword_memmap will contain both the model and the
 * scratch buffers.
 */

/* Specifies the required alignment for the hotword_memmap. */
extern const int kGoogleHotwordRequiredDataAlignment;

/*
 * Called to set up the Google hotword algorithm. Returns 1 if successful, and 0
 * otherwise.
 */
int GoogleHotwordDspInit(void *hotword_memmap);

/*
 * Call with every frame of samples to process. If a hotword is detected, this
 * function returns 1 otherwise 0. The required preamble length will be set to
 * the number of milliseconds of buffered audio to be transferred to the AP.
 */
int GoogleHotwordDspProcess(const void *samples, int num_samples,
			    int *preamble_length_ms);

/*
 * If there's a break in the audio stream (e.g. when Sound Activity Detection is
 * enabled), call this before any subsequent calls to GoogleHotwordDspProcess.
 */
void GoogleHotwordDspReset(void);

/* Returns the maximum possible audio preamble length in miliseconds. */
int GoogleHotwordDspGetMaximumAudioPreambleMs(void);

/* Returns an internal version number that this library was built at. */
extern int GoogleHotwordVersion(void);

#endif  /* SPEECH_MICRO_API_HOTWORD_DSP_API_H_ */
