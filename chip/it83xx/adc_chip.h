/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT83xx ADC module for Chrome EC */

#ifndef __CROS_EC_ADC_CHIP_H
#define __CROS_EC_ADC_CHIP_H

#include "common.h"

/*
 * Maximum time we allow for an ADC conversion.
 * NOTE:
 * This setting must be less than "SLEEP_SET_HTIMER_DELAY_USEC" in clock.c
 * or adding a sleep mask to prevent going in to deep sleep while ADC
 * converting.
 */
#define ADC_TIMEOUT_US 248

/* Minimum and maximum values returned by adc_read_channel(). */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 1023
#define ADC_MAX_MVOLT 3000

/* List of ADC channels. */
enum chip_adc_channel {
	CHIP_ADC_CH0 = 0,
	CHIP_ADC_CH1,
	CHIP_ADC_CH2,
	CHIP_ADC_CH3,
	CHIP_ADC_CH4,
	CHIP_ADC_CH5,
	CHIP_ADC_CH6,
	CHIP_ADC_CH7,
	CHIP_ADC_CH13,
	CHIP_ADC_CH14,
	CHIP_ADC_CH15,
	CHIP_ADC_CH16,
	CHIP_ADC_COUNT,
};

/* Data structure to define ADC channel control registers. */
struct adc_ctrl_t {
	volatile uint8_t *adc_ctrl;
	volatile uint8_t *adc_datm;
	volatile uint8_t *adc_datl;
	volatile uint8_t *adc_pin_ctrl;
};

/* Data structure to define ADC channels. */
struct adc_t {
	const char *name;
	int factor_mul;
	int factor_div;
	int shift;
	enum chip_adc_channel channel;
};

/*
 * Boards must provide this list of ADC channel definitions. This must match
 * the enum adc_channel list provided by the board.
 */
extern const struct adc_t adc_channels[];

#endif /* __CROS_EC_ADC_CHIP_H */
