/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ONLINE_CALIBRATION_H
#define __CROS_EC_ONLINE_CALIBRATION_H

#include "motion_sense.h"

/**
 * Initialize the online calibration caches.
 */
void online_calibration_init(void);

/**
 * Process a new data measurement from a given sensor.
 *
 * @param data Pointer to the data that should be processed.
 * @param sensor Pointer to the sensor that generated the data.
 * @param timestamp The time associated with the sample
 * @return EC_SUCCESS when successful.
 */
int online_calibration_process_data(
	struct ec_response_motion_sensor_data *data,
	struct motion_sensor_t *sensor,
	uint32_t timestamp);

#endif /* __CROS_EC_ONLINE_CALIBRATION_H */
