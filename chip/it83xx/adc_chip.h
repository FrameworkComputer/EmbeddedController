/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT8380 ADC module for Chrome EC */

#ifndef __CROS_EC_ADC_CHIP_H
#define __CROS_EC_ADC_CHIP_H

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
	int channel;
};

/*
 * Boards must provide this list of ADC channel definitions. This must match
 * the enum adc_channel list provided by the board.
 */
extern const struct adc_t adc_channels[];

#endif /* __CROS_EC_ADC_CHIP_H */
