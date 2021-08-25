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

/* List of ADC channels */
enum chip_adc_channel {
	CHIP_ADC_CH0 = 0,
	CHIP_ADC_CH1,
	CHIP_ADC_CH2,
	CHIP_ADC_CH3,
	CHIP_ADC_CH4,
	CHIP_ADC_CH5,
	CHIP_ADC_CH6,
	CHIP_ADC_CH7,
	CHIP_ADC_COUNT,
};

/* Minimum and maximum values returned by adc_read_channel(). */
#define ADC_READ_MIN 0
#ifdef CHIP_FAMILY_MEC172X
/* MEC172x ADC is 12BIT resolution in default */
#define ADC_READ_MAX 4095
#else
#define ADC_READ_MAX 1023
#endif

/* Just plain id mapping for code readability */
#define MCHP_ADC_CH(x) (x)

#endif /* __CROS_EC_ADC_CHIP_H */
