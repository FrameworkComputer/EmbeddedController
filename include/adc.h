/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADC interface for Chrome EC */

#ifndef __CROS_EC_ADC_H
#define __CROS_EC_ADC_H

#include "common.h"
#include "board.h"

/* Data structure to define ADC channels. */
struct adc_t
{
	const char* name;
	int sequencer;
	int factor_mul;
	int factor_div;
	int shift;
	int channel;
	int flag;
};

/* Read ADC channel. */
int adc_read_channel(enum adc_channel ch);

/* Read all ADC channels. Return non-zero on error. */
int adc_read_all_channels(int *data);

#endif  /* __CROS_EC_ADC_H */
