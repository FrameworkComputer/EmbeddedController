/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADC interface for Chrome EC */

#ifndef __CROS_EC_ADC_H
#define __CROS_EC_ADC_H

#include "common.h"

/**
 * Read an ADC channel.
 *
 * @param ch		Channel to read
 *
 * @return The scaled ADC value, or ADC_READ_ERROR if error.
 */
int adc_read_channel(enum adc_channel ch);

/**
 * Read all ADC channels.
 *
 * @param data		Destination array for channel data; must be
 *			ADC_CH_COUNT elements long.
 *
 * @return EC_SUCCESS, or non-zero on error.
 */
int adc_read_all_channels(int *data);

/**
 * Enable ADC watchdog. Note that interrupts might come in repeatedly very
 * quickly when ADC output goes out of the accepted range.
 *
 * @param ain_id	The AIN to be watched by the watchdog.
 * @param high		The high threshold that the watchdog would trigger
 *			an interrupt when exceeded.
 * @param low		The low threshold.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int adc_enable_watchdog(int ain_id, int high, int low);

/**
 * Disable ADC watchdog.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int adc_disable_watchdog(void);

#endif  /* __CROS_EC_ADC_H */
