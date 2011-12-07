/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADC interface for Chrome EC */

#ifndef __CROS_EC_ADC_H
#define __CROS_EC_ADC_H

#include "common.h"

/* Value returned by adc_read_*() methods if the read failed. */
#define ADC_READ_ERROR -1

/* Minimum and maximum values returned by adc_read(). */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 4095

/* ADC channels */
/* TODO: channel mapping is board-specific */
enum adc_channel {
	ADC_CH_POT = 0,
};

/* Initializes the module. */
int adc_init(void);

/* Reads an ADC channel.  Returns the ADC value, or ADC_READ_ERROR if error. */
int adc_read(enum adc_channel ch);

/* Returns the internal EC temperature in K, or ADC_READ_ERROR if error. */
int adc_read_ec_temperature(void);

#endif  /* __CROS_EC_ADC_H */
