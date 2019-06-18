/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32-specific ADC module for Chrome EC */

#ifndef __CROS_EC_ADC_CHIP_H
#define __CROS_EC_ADC_CHIP_H

#include "stdint.h"

enum stm32_adc_smpr {
	STM32_ADC_SMPR_DEFAULT = 0,
	STM32_ADC_SMPR_1_5_CY,
	STM32_ADC_SMPR_7_5_CY,
	STM32_ADC_SMPR_13_5_CY,
	STM32_ADC_SMPR_28_5_CY,
	STM32_ADC_SMPR_41_5_CY,
	STM32_ADC_SMPR_55_5_CY,
	STM32_ADC_SMPR_71_5_CY,
	STM32_ADC_SMPR_239_5_CY,
	STM32_ADC_SMPR_COUNT,
};

/* Data structure to define ADC channels. */
struct adc_t {
	const char *name;
	int factor_mul;
	int factor_div;
	int shift;
	int channel;
#ifdef CHIP_FAMILY_STM32F0
	enum stm32_adc_smpr sample_rate;  /* Sampling Rate of the channel */
#endif
};

/*
 * Boards must provide this list of ADC channel definitions.  This must match
 * the enum adc_channel list provided by the board. Also, for STM32F0, this
 * must be ordered by AIN ID.
 */
extern const struct adc_t adc_channels[];

/* Disable ADC module when we don't need it anymore. */
void adc_disable(void);

/* Minimum and maximum values returned by adc_read_channel(). */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 4095

/* Just plain id mapping for code readability */
#define STM32_AIN(x) (x)

#endif /* __CROS_EC_ADC_CHIP_H */
