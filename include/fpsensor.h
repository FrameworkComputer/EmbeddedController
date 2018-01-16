/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_H
#define __CROS_EC_FPSENSOR_H

#include <stdint.h>
#include "common.h"
#include "ec_commands.h"

#ifndef SPI_FP_DEVICE
#define SPI_FP_DEVICE (&spi_devices[0])
#endif

/*  Four-character-code */
#define FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
			   ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

/* 8-bit greyscale pixel format as defined by V4L2 headers */
#define V4L2_PIX_FMT_GREY FOURCC('G', 'R', 'E', 'Y')

/* --- fonctions provided by the sensor-specific driver --- */

/* Initialize the connected sensor hardware and put it in a low power mode. */
int fp_sensor_init(void);

/*
 * Fill the 'ec_response_fp_info' buffer with the sensor information
 * as required by the EC_CMD_FP_INFO host command.
 *
 * Put both the static information and the ones read from the sensor at runtime.
 */
int fp_sensor_get_info(struct ec_response_fp_info *resp);

/*
 * Put the sensor in its lowest power state.
 *
 * fp_sensor_configure_detect needs to be called to restore finger detection
 * functionality.
 */
void fp_sensor_low_power(void);

/*
 * Configure finger detection.
 *
 * Send the settings to the sensor, so it is properly configured to detect
 * the presence of a finger.
 */
void fp_sensor_configure_detect(void);

/*
 * Returns the status of the finger on the sensor.
 * (assumes fp_sensor_configure_detect was called before)
 */
enum finger_state {
	FINGER_NONE = 0,
	FINGER_PARTIAL = 1,
	FINGER_PRESENT = 2,
};
enum finger_state fp_sensor_finger_status(void);

/*
 * Acquires a fingerprint image.
 *
 * This function is called once the finger has been detected and cover enough
 * area of the sensor (ie fp_sensor_finger_status returned FINGER_PRESENT).
 * It does the acquisition immediately.
 * The image_data parameter points to an image data buffer of size
 *
 * FP_SENSOR_IMAGE_SIZE allocated by the caller.
 * Returns:
 * - 0 on success
 * - negative value on error
 * - FP_SENSOR_LOW_IMAGE_QUALITY on image captured but quality is too low
 * - FP_SENSOR_TOO_FAST on finger removed before image was captured
 * - FP_SENSOR_LOW_SENSOR_COVERAGE on sensor not fully covered by finger
 */
#define FP_SENSOR_LOW_IMAGE_QUALITY 1
#define FP_SENSOR_TOO_FAST 2
#define FP_SENSOR_LOW_SENSOR_COVERAGE 3
int fp_sensor_acquire_image(uint8_t *image_data);

/*
 * Acquires a fingerprint image with specific capture mode.
 *
 * Same as the fp_sensor_acquire_image function above,
 * excepted 'mode' can be set to one of the FP_CAPTURE_ constants
 * to get a specific image type (e.g. a pattern) rather than the default one.
 */
int fp_sensor_acquire_image_with_mode(uint8_t *image_data, int mode);

#endif /* __CROS_EC_FPSENSOR_H */
