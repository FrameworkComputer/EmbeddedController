/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LM4-specific ADC module for Chrome EC */

#ifndef __CROS_EC_LM4_ADC_H
#define __CROS_EC_LM4_ADC_H

enum lm4_adc_sequencer
{
	LM4_ADC_SEQ0 = 0,
	LM4_ADC_SEQ1,
	LM4_ADC_SEQ2,
	LM4_ADC_SEQ3,
	LM4_ADC_SEQ_COUNT
};

/* Data structure to define ADC channels. */
struct adc_t {
	const char *name;
	enum lm4_adc_sequencer sequencer;
	int factor_mul;
	int factor_div;
	int shift;
	int channel;
	int flag;
	uint32_t gpio_port;
	uint8_t gpio_mask;
};

extern const struct adc_t adc_channels[];

/* Minimum and maximum values returned by raw ADC read. */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 4095

/* Just plain id mapping for code readability */
#define LM4_AIN(x) (x)

/* Dummy value for "channel" in adc_t if we don't have an external channel. */
#define LM4_AIN_NONE (-1)

#endif /* __CROS_EC_LM4_ADC_H */
