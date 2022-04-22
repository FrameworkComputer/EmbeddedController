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
void fp_sensor_configure_detect(void);

enum finger_state {
	FINGER_NONE = 0,
	FINGER_PARTIAL = 1,
	FINGER_PRESENT = 2,
};

/**
 * Returns the status of the finger on the sensor.
 * (assumes fp_sensor_configure_detect was called before)
 *
 * @return finger_state
 */
enum finger_state fp_sensor_finger_status(void);

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

/**
 * Compares given finger image against enrolled templates.
 *
 * The matching algorithm can update the template with additional biometric data
 * from the image, if it chooses to do so.
 *
 * @param[in,out] templ a pointer to the array of template buffers.
 * @param templ_count the number of buffers in the array of templates.
 * @param[in] image the buffer containing the finger image
 * @param match_index index of the matched finger in the template array if any.
 * @param[out] update_bitmap contains one bit per template, the bit is set if
 * the match has updated the given template.
 *
 * @return EC_MKBP_FP_ERR_MATCH_NO on non-match
 * @return EC_MKBP_FP_ERR_MATCH_YES for match when template was not updated with
 * new data
 * @return EC_MKBP_FP_ERR_MATCH_YES_UPDATED for match when template was updated
 * @return EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED match, but update failed (not
 * saved)
 * @return EC_MKBP_FP_ERR_MATCH_LOW_QUALITY when matching could not be performed
 * due to low image quality
 * @return EC_MKBP_FP_ERR_MATCH_LOW_COVERAGE when matching could not be
 * performed due to finger covering too little area of the sensor
 * @return negative value on error
 */
int fp_finger_match(void *templ, uint32_t templ_count, uint8_t *image,
		    int32_t *match_index, uint32_t *update_bitmap);

/**
 * Start a finger enrollment session.
 *
 * @return 0 on success
 * @return negative value on error
 */
int fp_enrollment_begin(void);

/**
 * Generate a template from the finger whose enrollment has just being
 * completed.
 *
 * @param[out] templ template buffer, which can be set to NULL to abort current
 * enrollment process.
 *
 * @return 0 on success, negative value on error
 */
int fp_enrollment_finish(void *templ);

/**
 * Adds fingerprint image to the current enrollment session.
 *
 * @param[in] image Image to add to enrollment
 * @param[out] completion The percentage of the enrollment process that is
 * complete: [0-100].
 *
 * @return 0 on success
 * @return EC_MKBP_FP_ERR_ENROLL_OK when image was successfully enrolled
 * @return EC_MKBP_FP_ERR_ENROLL_IMMOBILE when image added, but user should be
 * advised to move finger
 * @return EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY when image could not be used due to
 * low image quality
 * @return EC_MKBP_FP_ERR_ENROLL_LOW_COVERAGE when image could not be used
 * due to finger covering too little area of the sensor
 */
int fp_finger_enroll(uint8_t *image, int *completion);

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

#endif /* __CROS_EC_FPSENSOR_H */
