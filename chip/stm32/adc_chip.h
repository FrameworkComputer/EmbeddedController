/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32-specific ADC module for Chrome EC */

#ifndef __CROS_EC_ADC_CHIP_H
#define __CROS_EC_ADC_CHIP_H

#ifdef CHIP_FAMILY_STM32L4
enum stm32_adc_smpr {
	STM32_ADC_SMPR_DEFAULT = 0,
	STM32_ADC_SMPR_2_5_CY,
	STM32_ADC_SMPR_6_5_CY,
	STM32_ADC_SMPR_12_5_CY,
	STM32_ADC_SMPR_24_5_CY,
	STM32_ADC_SMPR_47_5_CY,
	STM32_ADC_SMPR_92_5_CY,
	STM32_ADC_SMPR_247_5_CY,
	STM32_ADC_SMPR_640_5_CY,
	STM32_ADC_SMPR_COUNT,
};
#else
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
#endif

/* Data structure to define ADC channels. */
struct adc_t {
	const char *name;
	int factor_mul;
	int factor_div;
	int shift;
	int channel;
#ifdef CHIP_FAMILY_STM32L4
	int rank;
#endif

#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32L4)
	enum stm32_adc_smpr sample_rate;  /* Sampling Rate of the channel */
#endif
};

/* Disable ADC module when we don't need it anymore. */
void adc_disable(void);

/* Minimum and maximum values returned by adc_read_channel(). */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 4095

/* Just plain id mapping for code readability */
#define STM32_AIN(x) (x)

/* Add for ADCs with RANK */
#define STM32_RANK(x) (x)

#endif /* __CROS_EC_ADC_CHIP_H */
