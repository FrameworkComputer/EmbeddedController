/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_H
#define __CROS_EC_FPSENSOR_FPSENSOR_H

#include "common.h"
#include "ec_commands.h"
#include "fpsensor_alg.h"
#include "fpsensor_types.h"
#include "fpsensor_utils.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SPI_FP_DEVICE
#define SPI_FP_DEVICE (&spi_devices[0])
#endif

/*  Four-character-code */
#define FOURCC(a, b, c, d)                                              \
	((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | \
	 ((uint32_t)(d) << 24))

/* 8-bit greyscale pixel format as defined by V4L2 headers */
#define V4L2_PIX_FMT_GREY FOURCC('G', 'R', 'E', 'Y')

/* --- functions provided by the sensor-specific driver --- */

/**
 * Initialize the connected sensor hardware and put it in a low power mode.
 *
 * @return EC_SUCCESS always
 */
int fp_sensor_init(void);

/**
 * De-initialize the sensor hardware.
 *
 * @return 0 on success
 * @return negative value on error
 */
int fp_sensor_deinit(void);

/**
 * Fill the @p ec_response_fp_info buffer with the sensor information
 * as required by the EC_CMD_FP_INFO host command.
 *
 * Fills both the static information and information read from the sensor at
 * runtime.
 *
 * @param[out] resp sensor info
 *
 * @return EC_SUCCESS on success
 * @return EC_RES_ERROR on error
 */
int fp_sensor_get_info(struct ec_response_fp_info *resp);

/**
 * Put the sensor in its lowest power state.
 *
 * fp_sensor_configure_detect needs to be called to restore finger detection
 * functionality.
 */
void fp_sensor_low_power(void);

/**
 * Configure finger detection.
 *
 * Send the settings to the sensor, so it is properly configured to detect
 * the presence of a finger.
 */
void fp_configure_detect(void);

/**
 * Returns the status of the finger on the sensor.
 * (assumes fp_configure_detect was called before)
 *
 * @return finger_state
 */
enum finger_state fp_finger_status(void);

/**
 * Image captured but quality is too low
 */
#define FP_SENSOR_LOW_IMAGE_QUALITY 1
/**
 * Finger removed before image was captured
 */
#define FP_SENSOR_TOO_FAST 2

/**
 * Sensor not fully covered by finger
 */
#define FP_SENSOR_LOW_SENSOR_COVERAGE 3

/**
 * Acquires a fingerprint image.
 *
 * This function is called once the finger has been detected and cover enough
 * area of the sensor (i.e., fp_finger_status returned FINGER_PRESENT).
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
int fp_acquire_image(uint8_t *image_data);

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
int fp_acquire_image_with_mode(uint8_t *image_data, int mode);

/**
 * Runs a test for defective pixels.
 *
 * Should be triggered periodically by the client. The maintenance command can
 * take several hundred milliseconds to run.
 *
 * @return EC_ERROR_HW_INTERNAL on error (such as finger on sensor)
 * @return EC_SUCCESS on success
 */
int fp_maintenance(void);

#ifdef CONFIG_ZEPHYR
/**
 * Put the sensor into idle state
 *
 * This function is useful if it's necessary e.g. to leave 'detect' mode
 * due to timeout or user cancel.
 *
 * @return 0 on success
 * @return negative value on error
 */
int fp_idle(void);

#endif /* CONFIG_ZEPHYR */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_H */
