/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for LM4 chip */

#ifndef __CROS_EC_TEMP_SENSOR_CHIP_H
#define __CROS_EC_TEMP_SENSOR_CHIP_H

/**
 * Get the last polled value of the sensor.
 *
 * @param idx		Sensor index to read.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int chip_temp_sensor_get_val(int idx, int *temp_ptr);

#endif /* __CROS_EC_TEMP_SENSOR_CHIP_H */
