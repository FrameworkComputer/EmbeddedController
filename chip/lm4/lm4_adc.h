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

/* Minimum and maximum values returned by lm4_adc_flush_and_read(). */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 4095

/* Value returned if the read failed. */
#define ADC_READ_ERROR -1

/* Just plain id mapping for code readability */
#define LM4_AIN(x) (x)
#define LM4_AIN_NONE (-1)

/* Dummy value for "channel" in adc_t if we don't have an external channel. */
#define LM4_NO_AIN 0

/* Flush an ADC sequencer and initiate a read. Return raw ADC value. */
int lm4_adc_flush_and_read(enum lm4_adc_sequencer);

/* Configure an ADC sequencer to be dedicated for an ADC input "ain_id".
 * Value in "ssctl" field is passed to sampler sequencer control register.
 */
int lm4_adc_configure(enum lm4_adc_sequencer, int ain_id, int ssctl);

#endif /* __CROS_EC_LM4_ADC_H */
