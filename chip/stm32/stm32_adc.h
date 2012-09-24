/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32-specific ADC module for Chrome EC */

#ifndef __CROS_EC_STM32_ADC_H
#define __CROS_EC_STM32_ADC_H

extern const struct adc_t adc_channels[ADC_CH_COUNT];

/* Minimum and maximum values returned by adc_read_channel(). */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 4095

/* Value returned if the read failed. */
#define ADC_READ_ERROR -1

/* Just plain id mapping for code readability */
#define STM32_AIN(x) (x)

#endif /* __CROS_EC_STM32_ADC_H */
