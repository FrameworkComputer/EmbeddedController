/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CROS_EC_DRIVER_FINGERPRINT_ELAN_ELAN_SENSOR_H
#define CROS_EC_DRIVER_FINGERPRINT_ELAN_ELAN_SENSOR_H
#include "common.h"

/* Sensor pixel resolution */
#if defined(CONFIG_FP_SENSOR_ELAN80)
#define FP_SENSOR_IMAGE_SIZE (80 * 80)
#define FP_SENSOR_RES_X 80
#define FP_SENSOR_RES_Y 80
#if defined(CHIP_FAMILY_STM32F4)
#define FP_ALGORITHM_TEMPLATE_SIZE 15000
#elif defined(CHIP_FAMILY_STM32H7)
#define FP_ALGORITHM_TEMPLATE_SIZE 40960
#endif
#define FP_MAX_FINGER_COUNT 3
#elif defined(CONFIG_FP_SENSOR_ELAN515)
#define FP_SENSOR_IMAGE_SIZE (52 * 150)
#define FP_SENSOR_RES_X 52
#define FP_SENSOR_RES_Y 150
#if defined(CHIP_FAMILY_STM32F4)
#define FP_ALGORITHM_TEMPLATE_SIZE 15000
#elif defined(CHIP_FAMILY_STM32H7)
#define FP_ALGORITHM_TEMPLATE_SIZE 67000
#endif
#define FP_MAX_FINGER_COUNT 3
#endif
#define FP_SENSOR_RES_BPP (8)

/**
 * Set ELAN fingerprint sensor into finger touch detects and power saving mode
 *
 * @return 0 on success.
 *         negative value on error.
 */
int elan_woe_mode(void);

/**
 * Set ELAN fingerprint sensor into the image sensing mode
 *
 * @return 0 on success.
 *         negative value on error.
 */
int elan_sensing_mode(void);

/**
 * To initialize parameters of the ELAN matching algorithm
 *
 */
void algorithm_parameter_setting(void);

/**
 * Compares given finger image against enrolled templates.
 *
 * @param[in]  templ            a pointer to the array of template buffers.
 * @param[in]  templ_count      the number of buffers in the array of templates.
 * @param[in]  image            the buffer containing the finger image
 * @param[out] match_index      index of the matched finger in the template
 *                              array if any.
 * @param[out] update_bitmap    contains one bit per template, the bit is set if
 *                              the match has updated the given template.
 *
 * @return negative value on error, else one of the following code :
 * - EC_MKBP_FP_ERR_MATCH_NO on non-match
 * - EC_MKBP_FP_ERR_MATCH_YES for match when template was not updated with
 *   new data
 * - EC_MKBP_FP_ERR_MATCH_YES_UPDATED for match when template was updated
 * - EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED match, but update failed (not saved)
 * - EC_MKBP_FP_ERR_MATCH_LOW_QUALITY when matching could not be performed due
 *   to low image quality
 * - EC_MKBP_FP_ERR_MATCH_LOW_COVERAGE when matching could not be performed
 *   due to finger covering too little area of the sensor
 */
int elan_match(void *templ, uint32_t templ_count, uint8_t *image,
	       int32_t *match_index, uint32_t *update_bitmap);

/**
 * start a finger enrollment session and initialize enrollment data
 *
 * @return 0 on success.
 *
 */
int elan_enrollment_begin(void);

/**
 * Adds fingerprint image to the current enrollment session.
 *
 * @param[in]  image        fingerprint image data
 * @param[out] completion   retrieve percentage of current enrollment
 *
 * @return a negative value on error or one of the following codes:
 * - EC_MKBP_FP_ERR_ENROLL_OK when image was successfully enrolled
 * - EC_MKBP_FP_ERR_ENROLL_IMMOBILE when image added, but user should be
     advised to move finger
 * - EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY when image could not be used due to
 *   low image quality
 * - EC_MKBP_FP_ERR_ENROLL_LOW_COVERAGE when image could not be used due to
 *   finger covering too little area of the sensor
 */
int elan_enroll(uint8_t *image, int *completion);

/**
 * Acquires a fingerprint image with specific capture mode.
 *
 * @param[out] image_data   Memory buffer to retrieve fingerprint image data
 *                          Image_data is allocated by the caller and the size
 *                          is FP_SENSOR_IMAGE_SIZE.
 * @param[in]  mode         one of the FP_CAPTURE_ constants to get a
 *                          specific image type
 * - FP_CAPTURE_VENDOR_FORMAT: Full blown vendor-defined capture
 * - FP_CAPTURE_SIMPLE_IMAGE: Simple raw image capture
 * - FP_CAPTURE_PATTERN0: Self test pattern
 * - FP_CAPTURE_PATTERN1: Self test pattern
 * - FP_CAPTURE_QUALITY_TEST
 * - FP_CAPTURE_RESET_TEST
 * - FP_CAPTURE_TYPE_MAX
 *
 * @return
 * - 0 on success
 * - negative value on error
 * - FP_SENSOR_LOW_IMAGE_QUALITY on image captured but quality is too low
 * - FP_SENSOR_TOO_FAST on finger removed before image was captured
 * - FP_SENSOR_LOW_SENSOR_COVERAGE on sensor not fully covered by finger
 */
int elan_sensor_acquire_image_with_mode(uint8_t *image_data, int mode);

/**
 * Returns the status of the finger on the sensor.
 *
 * @return one of the following codes:
 * - FINGER_NONE
 * - FINGER_PARTIAL
 * - FINGER_PRESENT
 */
enum finger_state elan_sensor_finger_status(void);

/**
 * Generate a template from the finger whose enrollment has just being
 * completed.
 *
 * @param templ the buffer which will receive the template.
 *              templ can be set to NULL to abort the current enrollment
 *              process.
 *
 * @return 0 on success or a negative error code.
 */
int elan_enrollment_finish(void *templ);

/**
 * Fill the 'ec_response_fp_alg_info' buffer with the sensor alg information
 *
 * @param[out] resp      retrieve the algorithm information
 *
 * @return EC_SUCCESS on success otherwise error.
 */
int elan_sensor_get_alg_info(struct ec_response_fp_info *resp);

/**
 * Runs a test for defective pixels.
 *
 * Should be triggered periodically by the client. The maintenance command can
 * take several hundred milliseconds to run.
 *
 * @return EC_ERROR_HW_INTERNAL on error (such as finger on sensor)
 * @return EC_SUCCESS on success
 */
int elan_fp_maintenance(uint16_t *error_state);

/**
 * Deinitialize the sensor IC.
 *
 * @return EC_SUCCESS on success otherwise error.
 */
int elan_fp_deinit(void);
#endif
