/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* IT8xxx2 development board configuration */

#include "adc_chip.h"
#include "pwm.h"
#include "pwm_chip.h"

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = PWM_HW_CH_DCR7,
		.flags = 0,
		.freq_hz = 30000,
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_WITH_DSLEEP_FLAG] = {
		.channel = PWM_HW_CH_DCR0,
		.flags = PWM_CONFIG_DSLEEP,
		.freq_hz = 100,
		.pcfsr_sel = PWM_PRESCALER_C6,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/* Convert to mV (3000mV/1024). */
	[ADC_VBUSSA] = {
		.name = "ADC_VBUSSA",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH0, /* GPI0, ADC0 */
	},
	[ADC_VBUSSB] = {
		.name = "ADC_VBUSSB",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH1, /* GPI1, ADC1 */
	},
	[ADC_EVB_CH_13] = {
		.name = "ADC_EVB_CH_13",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH13, /* GPL1, ADC13 */
	},
	[ADC_EVB_CH_14] = {
		.name = "ADC_EVB_CH_14",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH14, /* GPL2, ADC14 */
	},
	[ADC_EVB_CH_15] = {
		.name = "ADC_EVB_CH_15",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH15, /* GPL3, ADC15 */
	},
	[ADC_EVB_CH_16] = {
		.name = "ADC_EVB_CH_16",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH16, /* GPL0, ADC16 */
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);
