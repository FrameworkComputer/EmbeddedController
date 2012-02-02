/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADC interface for Chrome EC */

#ifndef __CROS_EC_ADC_H
#define __CROS_EC_ADC_H

#include "common.h"
#include "board.h"
#include "gpio.h"

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

/* Initializes the module. */
int adc_init(void);

/* Read ADC channel. */
int adc_read_channel(enum adc_channel ch);

#endif  /* __CROS_EC_ADC_H */
