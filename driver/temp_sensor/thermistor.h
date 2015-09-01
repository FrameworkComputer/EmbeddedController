/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermistor module for Chrome EC */

#ifndef __CROS_EC_TEMP_SENSOR_THERMISTOR_H
#define __CROS_EC_TEMP_SENSOR_THERMISTOR_H

/**
 * ncp15wb temperature conversion routine.
 *
 * @param adc	10bit raw data on adc.
 *
 * @return	temperature in C.
 */
int ncp15wb_calculate_temp(uint16_t adc);

#endif  /* __CROS_EC_TEMP_SENSOR_THERMISTOR_NCP15WB_H */
