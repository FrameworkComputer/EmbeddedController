/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ONLINE_CALIBRATION_H
#define __CROS_EC_ONLINE_CALIBRATION_H

#include "motion_sense.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

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
int online_calibration_process_data(struct ec_response_motion_sensor_data *data,
				    struct motion_sensor_t *sensor,
				    uint32_t timestamp);

/**
 * Check if new calibration values are available since the last read.
 *
 * @return True if the calibration values have changed.
 */
bool online_calibration_has_new_values(void);

/**
 * Read available calibration values and reset dirty bits.
 *
 * @param sensor_num The sensor index to get the calibration values for.
 * @param out The target to copy the data into.
 * @return True if a value was written to `out`.
 */
bool online_calibration_read(struct motion_sensor_t *sensor,
			     struct ec_response_online_calibration_data *out);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ONLINE_CALIBRATION_H */
