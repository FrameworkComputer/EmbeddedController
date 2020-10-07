/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific ADC module for Chrome EC */

#ifndef __CROS_EC_ADC_CHIP_H
#define __CROS_EC_ADC_CHIP_H

#include "adc.h"

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
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
	NPCX_ADC_CH5,
	NPCX_ADC_CH6,
	NPCX_ADC_CH7,
	NPCX_ADC_CH8,
	NPCX_ADC_CH9,
#endif
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX9
	NPCX_ADC_CH10,
	NPCX_ADC_CH11,
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

/*
 * Boards may configure a ADC channel for use with thershold interrupts.
 * The threshold levels may be set from 0 to ADC_MAX_VOLT inclusive.
 */
struct npcx_adc_thresh_t {
	/* The ADC channel to monitor to generate threshold interrupts. */
	enum adc_channel adc_ch;

	/* Called when the interrupt fires */
	void (*adc_thresh_cb)(void);

	/* If set, threshold event is asserted when <= threshold level */
	int lower_or_higher;

	/* Desired threshold level in mV to assert. */
	int thresh_assert;
};

/**
 * Boards should call this function to register their threshold interrupt with
 * one of the threshold detectors. 'threshold_idx' is 1-based.
 *
 * @param threshold_idx - 1-based threshold detector index
 * @param thresh_cfg - Pointer to ADC threshold interrupt configuration
 */
void npcx_adc_register_thresh_irq(int threshold_idx,
				  const struct npcx_adc_thresh_t *thresh_cfg);

/**
 * Configure an ADC channel for repetitive conversion.
 *
 * If you are using ADC threshold interrupts and the need is timing critical,
 * you will want to enable this on the ADC channels you have configured for
 * threshold interrupts.
 *
 * NOTE: Enabling this will prevent the EC from entering deep sleep and will
 * increase power consumption!
 *
 * @param input_ch - The ADC channel you wish to configure
 * @param enable   - 1 to enable, 0 to disable
 */
void npcx_set_adc_repetitive(enum npcx_adc_input_channel input_ch, int enable);

/**
 * Enable/Disable ADC threshold detector interrupt.
 *
 * @param threshold_idx - 1-based threshold detector index
 * @param enable         - 1 to enable, 0 to disable
 */
void npcx_adc_thresh_int_enable(int threshold_idx, int enable);

/**
 * Return the ADC value from CHNDAT register directly when the channel is
 * configured in the repetitive mode.
 *
 * @param   input_ch    channel number
 * @return  ADC data
 */
int adc_read_data(enum npcx_adc_input_channel input_ch);
#endif /* __CROS_EC_ADC_CHIP_H */
