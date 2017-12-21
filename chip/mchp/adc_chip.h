/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MCHP MEC specific ADC module for Chrome EC */

#ifndef __CROS_EC_ADC_CHIP_H
#define __CROS_EC_ADC_CHIP_H

/* Data structure to define ADC channels. */
struct adc_t {
	const char *name;
	int factor_mul;
	int factor_div;
	int shift;
	int channel;
};

/*
 * Boards must provide this list of ADC channel definitions.
 * This must match the enum adc_channel list provided by the board.
 */
extern const struct adc_t adc_channels[];

/* Minimum and maximum values returned by adc_read_channel(). */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 1023

/* Just plain id mapping for code readability */
#define MCHP_ADC_CH(x) (x)

#endif /* __CROS_EC_ADC_CHIP_H */
