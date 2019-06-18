/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific ADC module for Chrome EC */

#ifndef __CROS_EC_ADC_CHIP_H
#define __CROS_EC_ADC_CHIP_H

/* Minimum and maximum values returned by raw ADC read. */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 1023
#define ADC_MAX_VOLT 2816

/* ADC input channel select */
enum npcx_adc_input_channel {
	NPCX_ADC_CH0 = 0,
	NPCX_ADC_CH1,
	NPCX_ADC_CH2,
	NPCX_ADC_CH3,
	NPCX_ADC_CH4,
#if defined(CHIP_FAMILY_NPCX7)
	NPCX_ADC_CH5,
	NPCX_ADC_CH6,
	NPCX_ADC_CH7,
	NPCX_ADC_CH8,
	NPCX_ADC_CH9,
#endif
	NPCX_ADC_CH_COUNT
};

/* Data structure to define ADC channels. */
struct adc_t {
	const char *name;
	enum npcx_adc_input_channel input_ch;
	int factor_mul;
	int factor_div;
	int shift;
};

/*
 * Boards must provide this list of ADC channel definitions.  This must match
 * the enum adc_channel list provided by the board.
 */
extern const struct adc_t adc_channels[];

#endif /* __CROS_EC_ADC_CHIP_H */
