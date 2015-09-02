/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ec_adc which uses adc and thermistors module for Chrome EC
 * Some EC has it's own ADC modules, define here EC's max ADC channels.
 * We can consider every channel as a thermal sensor.
 */

#ifndef __CROS_EC_TEMP_SENSOR_EC_ADC_H
#define __CROS_EC_TEMP_SENSOR_EC_ADC_H

/**
 * Get the latest value from the sensor.
 *
 * @param idx		ADC channel to read.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int ec_adc_get_val(int idx, int *temp_ptr);

#endif  /* __CROS_EC_TEMP_SENSOR_EC_ADC_H */
