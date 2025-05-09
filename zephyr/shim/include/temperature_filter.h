/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Low pass filter for on die temperature
 */
#ifndef __CROS_EC_TEMPERATURE_FILTER_H
#define __CROS_EC_TEMPERATURE_FILTER_H

#include "temp_sensor/temp_sensor.h"

/**
 * Check whether the temp sensor coefficients is zero or not.
 *
 * @param id temp sensor id
 * @return true if support (coefficients none zero)
 */
bool temperature_filter_is_support(enum temp_sensor_id id);

/**
 * Return the virtual temperature.
 *
 * @param id temp sensor id
 * @param temp_ptr the actual temperature value from the physical sensor
 * @return true if support (coefficients none zero)
 */
void temperature_filter_get_temp(enum temp_sensor_id id, int *temp_ptr);

/**
 * Calculate the virtual temperature with the coefficients.
 *
 * @param id temp sensor id
 * @param temp_ptr the actual temperature value from the physical sensor
 * @return true if support (coefficients none zero)
 */
void temperature_filter_update_temp(enum temp_sensor_id id, int temp_ptr);

#endif /* __CROS_EC_TEMPERATURE_FILTER_H */
