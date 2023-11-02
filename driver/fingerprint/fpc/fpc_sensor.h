/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_FPC_FPC_SENSOR_H_
#define __CROS_EC_DRIVER_FINGERPRINT_FPC_FPC_SENSOR_H_

#include "common.h"

#if defined(CONFIG_FP_SENSOR_FPC1025)
#include "bep/fpc1025_private.h"
#elif defined(CONFIG_FP_SENSOR_FPC1035)
#include "bep/fpc1035_private.h"
#elif defined(CONFIG_FP_SENSOR_FPC1145)
#include "libfp/fpc1145_private.h"
#else
#error "Sensor type not defined!"
#endif

#include "fpsensor/fpsensor_types.h"

/**
 * Runs a test for defective pixels.
 *
 * Should be triggered periodically by the client. The maintenance command can
 * take several hundred milliseconds to run.
 *
 * @return EC_ERROR_HW_INTERNAL on error (such as finger on sensor)
 * @return EC_SUCCESS on success
 */
int fpc_fp_maintenance(uint16_t *error_state);

/**
 * Configure finger detection.
 *
 * Send the settings to the sensor, so it is properly configured to detect
 * the presence of a finger.
 */
void fp_sensor_configure_detect(void);

/**
 * Returns the status of the finger on the sensor.
 * (assumes fp_sensor_configure_detect was called before)
 *
 * @return finger_state
 */
enum finger_state fp_sensor_finger_status(void);

/**
 * Acquires a fingerprint image.
 *
 * This function is called once the finger has been detected and cover enough
 * area of the sensor (i.e., fp_sensor_finger_status returned FINGER_PRESENT).
 * It does the acquisition immediately.
 *
 * @param[out] image_data Image from sensor. Buffer must be allocated by
 * caller with size FP_SENSOR_IMAGE_SIZE.
 *
 * @return 0 on success
 * @return negative value on error
 * @return FP_SENSOR_LOW_IMAGE_QUALITY on image captured but quality is too low
 * @return FP_SENSOR_TOO_FAST on finger removed before image was captured
 * @return FP_SENSOR_LOW_SENSOR_COVERAGE on sensor not fully covered by finger
 */
int fp_sensor_acquire_image(uint8_t *image_data);

/**
 * Acquires a fingerprint image with specific capture mode.
 *
 * Same as the fp_sensor_acquire_image function(),
 * except @p mode can be set to one of the fp_capture_type constants
 * to get a specific image type (e.g. a pattern) rather than the default one.
 *
 * @param[out] image_data Image from sensor. Buffer must be allocated by
 * caller with size FP_SENSOR_IMAGE_SIZE.
 * @param mode  enum fp_capture_type
 *
 * @return 0 on success
 * @return negative value on error
 */
int fp_sensor_acquire_image_with_mode(uint8_t *image_data, int mode);

#endif /* __CROS_EC_DRIVER_FINGERPRINT_FPC_FPC_SENSOR_H_ */
