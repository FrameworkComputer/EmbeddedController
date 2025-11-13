/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Private sensor interface */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_ELAN80SERIES_PRIVATE_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_ELAN80SERIES_PRIVATE_H_

#include <stdint.h>

/* ELAN error codes */
enum elan_error_code {
	ELAN_ERROR_NONE = 0,
	ELAN_ERROR_SPI = 1,
	ELAN_ERROR_SCAN = 2,
	ELAN_ERROR_CAL = 3,
	ELAN_ERROR_DEFECT_NUM = 4,
	ELAN_ERROR_DEFECT_X = 5,
	ELAN_ERROR_DEFECT_Y = 6
};

/* External capture types from ELAN's sensor library */
enum elan_capture_type {
	ELAN_CAPTURE_TYPE_INVALID = -1,
	ELAN_CAPTURE_VENDOR_FORMAT = 0,
	ELAN_CAPTURE_SIMPLE_IMAGE = 1,
	ELAN_CAPTURE_PATTERN0 = 2,
	ELAN_CAPTURE_PATTERN1 = 3,
	ELAN_CAPTURE_QUALITY_TEST = 4,
	ELAN_CAPTURE_RESET_TEST = 5,
};

/* ELAN error info */
typedef struct {
	uint32_t num_defective_pixels;
	uint16_t sensor_error_code;
} fp_sensor_info_t;

/**
 * Get the fingerprint sensor HWID.
 */
int elan_get_hwid(uint16_t *id);

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
void elan_alg_param_setting(void);

/**
 * Execute reset ELAN fingerprint sensor flow.
 */
void elan_execute_reset(void);

/**
 * To calibrate ELAN fingerprint sensor and keep the calibration results
 * for correcting fingerprint image data
 *
 * @return 0 on success.
 *         negative value on error.
 */
int elan_calibration(void);

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
 * Update the enrolled template.
 *
 * @param[in]  templ            a pointer to the array of template buffers.
 * @param[in]  match_index      index of the matched finger in the template
 *                              array if any.
 *
 * @return negative value on error, else one of the following code :
 * - EC_MKBP_FP_ERR_MATCH_YES_UPDATED if template was updated with new data
 * - EC_MKBP_FP_ERR_MATCH_YES if template was not updated with new data
 */
int elan_template_update(void *templ, int32_t match_index);

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

/** Image captured. */
#define FP_SENSOR_GOOD_IMAGE_QUALITY 0
/** Image captured but quality is too low. */
#define FP_SENSOR_LOW_IMAGE_QUALITY 1
/** Finger removed before image was captured. */
#define FP_SENSOR_TOO_FAST 2
/** Sensor not fully covered by finger. */
#define FP_SENSOR_LOW_COVERAGE 3

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

enum finger_state {
	FINGER_NONE = 0, /**< No finger detected. */
	FINGER_PARTIAL = 1, /**< Partial finger detected. */
	FINGER_PRESENT = 2, /**< Finger fully present on the sensor. */
};

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
 * Runs a test for defective pixels.
 *
 * Unused by staticlib.
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

/**
 * Use setting flash addresses to access ELAN fingerprint data.
 *
 * @param base_addr    Base address of the fingerprint base image in flash.
 * @param ft_info_addr Address of the fingerprint info in flash.
 *
 */
void use_flash_addresses(uint32_t base_addr, uint32_t ft_info_addr);

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_ELAN80SERIES_PRIVATE_H_ */
