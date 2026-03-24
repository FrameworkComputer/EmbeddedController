/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Private sensor interface */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_EGIS630_PRIVATE_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_EGIS630_PRIVATE_H_

#include <stddef.h>
#include <stdint.h>

typedef enum {
	EGIS_API_OK = 0,
	EGIS_API_IMAGE_QUALITY_GOOD = 0,
	EGIS_API_IMAGE_QUALITY_BAD = 1,
	EGIS_API_IMAGE_QUALITY_WATER = 2,
	EGIS_API_IMAGE_QUALITY_PARTIAL = 3,
	EGIS_API_IMAGE_EMPTY = 4,
	EGIS_API_FINGER_PRESENT = 5,
	EGIS_API_FINGER_LOST = 6,
	EGIS_API_ENROLL_FINISH = 11,
	EGIS_API_ENROLL_IMAGE_OK = 12,
	EGIS_API_ENROLL_REDUNDANT_INPUT = 13,
	EGIS_API_ENROLL_LOW_QUALITY = 14,
	EGIS_API_ENROLL_LOW_COVERAGE = 15,
	EGIS_API_MATCH_MATCHED = 21,
	EGIS_API_MATCH_MATCHED_UPDATED = 22,
	EGIS_API_MATCH_MATCHED_UPDATED_FAILED = 23,
	EGIS_API_MATCH_NOT_MATCHED = 24,
	EGIS_API_MATCH_LOW_QUALITY = 25,
	EGIS_API_MATCH_LOW_COVERAGE = 26,
	EGIS_API_ERROR_GENERAL = -1,
	EGIS_API_ERROR_COMMAND_FAIL = -2,
	EGIS_API_ERROR_DEVICE_NOT_FOUND = -3,
	EGIS_API_ERROR_MEMORY = -4,
	EGIS_API_ERROR_PARAMETER = -5,
	EGIS_API_ERROR_WRONG_STATE = -6,
	EGIS_API_ERROR_IO_SPI = -7,
	EGIS_API_ERROR_SENSOR_GENERAL = -11,
	EGIS_API_ERROR_SENSOR_SENSING_MDOE_CALIBRATION = -12,
	EGIS_API_ERROR_SENSOR_DETECT_MDOE_CALIBRATION = -13,
	EGIS_API_ERROR_SENSOR_NEED_RESET = -15,
	EGIS_API_ERROR_SENSOR_OCP_DETECT = -16,
	EGIS_API_ERROR_FINGER_UNSTABLE = -17,
	EGIS_API_ERROR_MATCHER_LIB_FAIL = -18,
	EGIS_API_ERROR_EMFP_LIB_FAIL = -19,
	EGIS_API_ERROR = -20,
	EGIS_API_ERROR_NOT_SUPPORTED = -21,
} egis_api_return_t;

typedef struct FP_EGIS_IMAGE FP_EGIS_IMAGE;

/**
 * enum egis_capture_mode_t - Specifies the "mode" when capturing images.
 *
 * @EGIS_CAPTURE_VENDOR_FORMAT: Capture normal images with 8-bits per pixel
 * output
 * @EGIS_CAPTURE_BLACK_PXL_TEST: Capture for check dark pixel test with 16-bits
 * per pixel output
 * @EGIS_CAPTURE_WHITE_PXL_TEST: Capture for check white pixel test with 16-bits
 * per pixel output
 * @EGIS_CAPTURE_DEFECT_PXL_TEST: Capture for check defect pixel test with
 * 16-bits per pixel output
 * @EGIS_CAPTURE_NOISE_TEST: Capture for check noise test with 16-bits per pixel
 * output
 * @EGIS_CAPTURE_ABNORMAL_TEST: Capture for check abnormal pixel test with
 * 16-bits per pixel output
 * @EGIS_CAPTURE_RV_INT_TEST: Capture for RV test with 16-bits per pixel output
 * @EGIS_CAPTURE_TYPE_MAX: End of enum
 *
 */
typedef enum {
	EGIS_CAPTURE_TYPE_INVALID = -1,
	EGIS_CAPTURE_NORMAL_FORMAT = 0,
	EGIS_CAPTURE_BLACK_PXL_TEST = 1,
	EGIS_CAPTURE_WHITE_PXL_TEST = 2,
	EGIS_CAPTURE_DEFECT_PXL_TEST = 3,
	EGIS_CAPTURE_NOISE_TEST = 4,
	EGIS_CAPTURE_ABNORMAL_TEST = 5,
	EGIS_CAPTURE_RV_INT_TEST = 6,
	EGIS_CAPTURE_IMAGE_COLLECTION = 7,
	EGIS_CAPTURE_TYPE_MAX,
} egis_capture_mode_t;

/**
 * @brief Get the fingerprint sensor HWID
 *
 * @param[out] id sensor id read from sensor.
 *
 * @return EGIS_API_OK : on success.
 * @return EGIS_API_ERROR_IO_SPI on SPI transfer failure, @p id filled with
 * zeros
 * @return EGIS_API_ERROR_PARAMETER on incorrect parameter, @p id filled with
 * zeros
 */
egis_api_return_t egis_get_hwid(uint16_t *id);

/**
 * @brief Reset and initialize the sensor IC.
 *
 * @return EGIS_API_OK : on success
 * @return negative value on error, list below
 * @return EGIS_API_ERROR_DEVICE_NOT_FOUND : on sensor cannot be detected
 * @return EGIS_API_ERROR_IO_SPI : on execute SPI transfer fail
 * @return EGIS_API_ERROR_SENSOR_OCP_DETECT : on sensor OCP detect
 * @return EGIS_API_ERROR_SENSOR_NEED_RESET : on sensor need reset
 * @return EGIS_API_ERROR_SENSOR_SENSING_MDOE_CALIBRATION : on sensor
 * calibration sensing mode fail
 * @return EGIS_API_ERROR_SENSOR_DETECT_MDOE_CALIBRATION : on sensor calibration
 * detect mode fail
 * @return EGIS_API_ERROR_MEMORY : on alloc memory fail
 * @return EGIS_API_ERROR_GENERAL : on other operation fail
 * @return EGIS_API_ERROR_SENSOR_GENERAL : on sensor operation fail
 * @return EGIS_API_ERROR_PARAMETER : on incorrect parameter
 */
egis_api_return_t egis_sensor_init(void);

/**
 * @brief Deinitialize the sensor IC.
 *
 * @return EGIS_API_OK : on success.
 */
egis_api_return_t egis_sensor_deinit(void);

/**
 * Power down the sensor IC.
 *
 */
void egis_sensor_power_down(void);

/**
 * @brief Acquire a fingerprint image with specific capture mode.
 *
 * @param[out] image_data Image from sensor. The buffer size must meet the
 * requirement of fp_capture_type.
 * @param[in] buf_size  The buffer size of image_data.
 * @param[in] mode  enum fp_capture_type.
 *
 * @return EGIS_API_IMAGE_QUALITY_GOOD : on success
 * @return EGIS_API_IMAGE_QUALITY_BAD : on image captured but quality is too low
 * @return EGIS_API_IMAGE_QUALITY_WATER : on image captured but image may had
 * water
 * @return EGIS_API_IMAGE_QUALITY_PARTIAL : on sensor not fully covered by
 * finger
 * @return EGIS_API_IMAGE_EMPTY : on finger removed before image was captured
 * @return negative value on error, list below
 * @return EGIS_API_ERROR_MEMORY : on alloc memory fail
 * @return EGIS_API_ERROR_SENSOR_OCP_DETECT : on sensor OCP detect
 * @return EGIS_API_ERROR_SENSOR_NEED_RESET : on sensor need reset
 * @return EGIS_API_ERROR_PARAMETER : on incorrect parameter
 * @return EGIS_API_ERROR_IO_SPI : on execute SPI transfer fail
 */
egis_api_return_t egis_get_image_with_mode(uint8_t *image_data,
					   uint32_t buf_size, int mode);

/**
 * @brief Set the finger detection mode for the Egis sensor.
 *
 */
void egis_set_detect_mode(void);

/**
 * @brief Check the sensor interrupt status.
 *
 * @return EGIS_API_FINGER_PRESENT when the finger is present. The capacitive
 * sensor has detected an increase in the capacitance value on the suraface,
 * which exceeds the threshold set for finger presence.
 * @return EGIS_API_FINGER_LOST when the finger has been removed. The capacitive
 * sensor has detected an decrease in the capacitance value on the suraface,
 * which exceeds the threshold set for finger removal.
 * @return negative value on error, list below
 * @return EGIS_API_ERROR_SENSOR_OCP_DETECT : on sensor OCP detect
 * @return EGIS_API_ERROR_PARAMETER : on incorrect parameter
 * @return EGIS_API_ERROR_IO_SPI : on execute SPI transfer fail
 */
egis_api_return_t egis_check_int_status(void);

/**
 * Compares given finger image against enrolled templates.
 *
 * The matching algorithm can update the template with additional biometric data
 * from the image, if it chooses to do so.
 *
 * @param[in,out] templ a pointer to the array of template buffers.
 * @param templ_count the number of buffers in the array of templates.
 * @param[in] imo the buffer containing the finger image object
 * @param match_index index of the matched finger in the template array if any.
 * @param[out] update_bitmap Optional. Contains one bit per template, the bit
 * is set if the match has updated the given template. If update_bitmap is
 * NULL, updates are denied even if a match occurs.
 *
 * @return EGIS_API_MATCH_NOT_MATCHED on non-match
 * @return EGIS_API_MATCH_MATCHED for match when template was not updated with
 * new data
 * @return EGIS_API_MATCH_MATCHED_UPDATED for match when template was updated
 * @return EGIS_API_MATCH_MATCHED_UPDATED_FAILED match, but update failed (not
 * saved)
 * @return EGIS_API_MATCH_LOW_QUALITY when matching could not be performed due
 * to low image quality
 * @return EGIS_API_MATCH_LOW_COVERAGE when matching could not be performed due
 * to finger covering too little area of the sensor
 * @return negative value on error, list below
 * @return EGIS_API_ERROR_PARAMETER : on incorrect parameter
 * @return EGIS_API_ERROR_MATCHER_LIB_FAIL : on matcher lib fail
 * @return EGIS_API_ERROR_EMFP_LIB_FAIL : on emfp lib fail
 */
egis_api_return_t egis_finger_match(void *templ, uint32_t templ_count,
				    FP_EGIS_IMAGE *imo, int32_t *match_index,
				    uint32_t *update_bitmap);

/**
 * Start a finger enrollment session.
 *
 * @return 0 on success
 * @return negative value on error, list below
 * @return EGIS_API_ERROR_MATCHER_LIB_FAIL : on matcher lib fail
 */
egis_api_return_t egis_enrollment_begin(void);

/**
 * Generate a template from the finger whose enrollment has just being
 * completed.
 *
 * @param[out] templ template buffer, which can be set to NULL to abort current
 * enrollment process.
 *
 * @return negative value on error, list below
 * @return EGIS_API_ERROR : on fail.
 */
egis_api_return_t egis_enrollment_finish(void *templ);

/**
 * Adds fingerprint image to the current enrollment session.
 *
 * @param[in] imo Image object to add to enrollment
 * @param[out] completion The percentage of the enrollment process that is
 * complete: [0-100].
 *
 * @return 0 on success
 * @return EGIS_API_ENROLL_FINISH when image was successfully enrolled and
 * enroll_percentage 100%
 * @return EGIS_API_ENROLL_IMAGE_OK when image was successfully enrolled
 * @return EGIS_API_ENROLL_REDUNDANT_INPUT when image added, but user should be
 * advised to move finger
 * @return EGIS_API_ENROLL_LOW_QUALITY when image could not be used due to low
 * image quality
 * @return EGIS_API_ENROLL_LOW_COVERAGE when image could not be used due to
 * finger covering too little area of the sensor
 * @return negative value on error, list below
 * @return EGIS_API_ERROR_PARAMETER : on incorrect parameter
 * @return EGIS_API_ERROR_MATCHER_LIB_FAIL : on matcher lib fail
 * @return EGIS_API_ERROR_EMFP_LIB_FAIL : on emfp lib fail
 */
egis_api_return_t egis_finger_enroll(FP_EGIS_IMAGE *imo, int *completion);

/**
 * apply sensor calibration from storage and do re-calibration.
 * @param[in] data_addr calibration data addr
 * @param[in] data_len total calibration size
 *
 * @return EGIS_API_OK : on success
 * @return negative value on error, list below
 * @return EGIS_API_ERROR_DEVICE_NOT_FOUND : on sensor cannot be detected
 * @return EGIS_API_ERROR_IO_SPI : on execute SPI transfer fail
 * @return EGIS_API_ERROR_SENSOR_OCP_DETECT : on sensor OCP detect
 * @return EGIS_API_ERROR_SENSOR_NEED_RESET : on sensor need reset
 * @return EGIS_API_ERROR_SENSOR_SENSING_MDOE_CALIBRATION : on sensor
 * calibration sensing mode fail
 * @return EGIS_API_ERROR_SENSOR_DETECT_MDOE_CALIBRATION : on sensor calibration
 * detect mode fail
 * @return EGIS_API_ERROR_MEMORY : on alloc memory fail
 * @return EGIS_API_ERROR_GENERAL : on other operation fail
 * @return EGIS_API_ERROR_SENSOR_GENERAL : on sensor operation fail
 * @return EGIS_API_ERROR_PARAMETER : on incorrect parameter
 * @return EGIS_API_ERROR_NOT_SUPPORTED : when the feature is not supported by
 * the library
 */
egis_api_return_t egis_apply_calibration_data(uint8_t *data_addr,
					      uint32_t data_len);

/**
 * Returns the status of the finger on the sensor.
 * (assumes fp_sensor_configure_detect was called before)
 *
 * @return finger_state
 */
int fp_sensor_finger_status(void);

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_EGIS630_PRIVATE_H_ */
