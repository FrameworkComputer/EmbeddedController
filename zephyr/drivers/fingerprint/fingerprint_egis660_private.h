/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Private sensor interface */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_EGIS660_PRIVATE_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_EGIS660_PRIVATE_H_

#include <stdint.h>

/* External capture types from EGIS's sensor library */
enum egis_capture_type {
	EGIS_CAPTURE_VENDOR_FORMAT = 0,
	EGIS_CAPTURE_SIMPLE_IMAGE = 1,
	EGIS_CAPTURE_PATTERN0 = 2,
	EGIS_CAPTURE_PATTERN1 = 3,
	EGIS_CAPTURE_QUALITY_TEST = 4,
	EGIS_CAPTURE_RESET_TEST = 5,
};

/** @brief Common results returned by BEP functions.
 *
 * BEP config/usage errors:
 * Examples: Incorrect arguments/parameters when calling BEP API
 * functions; functions called in incorrect order.
 * Action: Fix SW bug.
 * EGIS_BEP_RESULT_GENERAL_ERROR
 * EGIS_BEP_RESULT_NOT_IMPLEMENTED
 * EGIS_BEP_RESULT_NOT_SUPPORTED
 * EGIS_BEP_RESULT_NOT_INITIALIZED
 * EGIS_BEP_RESULT_CANCELLED
 * EGIS_BEP_RESULT_NO_RESOURCE
 * EGIS_BEP_RESULT_WRONG_STATE
 * EGIS_BEP_RESULT_ID_NOT_UNIQUE
 * EGIS_BEP_RESULT_ID_NOT_FOUND
 * EGIS_BEP_RESULT_INVALID_FORMAT
 * EGIS_BEP_RESULT_INVALID_ARGUMENT
 * EGIS_BEP_RESULT_INVALID_PARAMETER
 * EGIS_BEP_RESULT_INVALID_CALIBRATION
 * EGIS_BEP_RESULT_MISSING_TEMPLATE
 * EGIS_BEP_RESULT_STORAGE_NOT_FORMATTED
 * EGIS_BEP_RESULT_SENSOR_NOT_INITIALIZED
 * EGIS_BEP_RESULT_SENSOR_MISMATCH
 * EGIS_BEP_RESULT_CRYPTO_ERROR
 *
 * Dynamic memory/heap errors:
 * Examples: Memory leak; heap is too small.
 * Action: Fix SW bug.
 * EGIS_BEP_RESULT_NO_MEMORY
 *
 * Sensor and communication errors:
 * Examples: Broken sensor communication lines; unstable power supply.
 * Action: Fix HW bug.
 * EGIS_BEP_RESULT_BROKEN_SENSOR
 * EGIS_BEP_RESULT_INTERNAL_ERROR
 * EGIS_BEP_RESULT_TIMEOUT
 * EGIS_BEP_RESULT_IO_ERROR
 *
 * Image capture errors:
 * Examples: Finger removed from sensor too quickly.
 * Action: Call the function again.
 * EGIS_BEP_RESULT_IMAGE_CAPTURE_ERROR
 */

enum egis_bep_result {
	/** No errors occurred. */
	EGIS_BEP_RESULT_OK = 0,
	/** General error. */
	EGIS_BEP_RESULT_GENERAL_ERROR = -1,
	/** Internal error. */
	EGIS_BEP_RESULT_INTERNAL_ERROR = -2,
	/** Invalid argument. */
	EGIS_BEP_RESULT_INVALID_ARGUMENT = -3,
	/** The functionality is not implemented. */
	EGIS_BEP_RESULT_NOT_IMPLEMENTED = -4,
	/** The operation was cancelled. */
	EGIS_BEP_RESULT_CANCELLED = -5,
	/** Out of memory. */
	EGIS_BEP_RESULT_NO_MEMORY = -6,
	/** Resources are not available. */
	EGIS_BEP_RESULT_NO_RESOURCE = -7,
	/** An I/O error occurred. */
	EGIS_BEP_RESULT_IO_ERROR = -8,
	/** Sensor is broken. */
	EGIS_BEP_RESULT_BROKEN_SENSOR = -9,
	/** The operation cannot be performed in the current state. */
	EGIS_BEP_RESULT_WRONG_STATE = -10,
	/** The operation timed out. */
	EGIS_BEP_RESULT_TIMEOUT = -11,
	/** The ID is not unique. */
	EGIS_BEP_RESULT_ID_NOT_UNIQUE = -12,
	/** The ID is not found. */
	EGIS_BEP_RESULT_ID_NOT_FOUND = -13,
	/** The format is invalid. */
	EGIS_BEP_RESULT_INVALID_FORMAT = -14,
	/** An image capture error occurred. */
	EGIS_BEP_RESULT_IMAGE_CAPTURE_ERROR = -15,
	/** Sensor hardware id or sensor configuration mismatch. */
	EGIS_BEP_RESULT_SENSOR_MISMATCH = -16,
	/** Invalid parameter. */
	EGIS_BEP_RESULT_INVALID_PARAMETER = -17,
	/** Missing Template. */
	EGIS_BEP_RESULT_MISSING_TEMPLATE = -18,
	/** Invalid Calibration.*/
	EGIS_BEP_RESULT_INVALID_CALIBRATION = -19,
	/** Calibration/template storage not formatted.*/
	EGIS_BEP_RESULT_STORAGE_NOT_FORMATTED = -20,
	/** Sensor hasn't been initialized. */
	EGIS_BEP_RESULT_SENSOR_NOT_INITIALIZED = -21,
	/** Enroll fail after too many bad images. */
	EGIS_BEP_RESULT_TOO_MANY_BAD_IMAGES = -22,
	/** Cryptographic operation failed. */
	EGIS_BEP_RESULT_CRYPTO_ERROR = -23,
	/** The functionality is not supported. */
	EGIS_BEP_RESULT_NOT_SUPPORTED = -24,
	/** Finger not stable. */
	EGIS_BEP_RESULT_FINGER_NOT_STABLE = -25,
	/** The functionality could not be used before it's initialized. */
	EGIS_BEP_RESULT_NOT_INITIALIZED = -26,
};

/* EGIS specific initialization and de-initialization functions */
int fp_sensor_open(void);
int fp_sensor_close(void);

/* Get sensor hardware ID.*/
int fp_sensor_get_hwid(uint16_t *id);

/* Configure sensor to deepsleep mode.*/
int fp_sensor_deepsleep(void);

/* Get EGIS library version code.*/
const char *fp_sensor_get_version(void);

/* Get EGIS library build info.*/
const char *fp_sensor_get_build_info(void);

struct egis660_fp_sensor_info {
	uint32_t num_defective_pixels;
};

/**
 * fp_sensor_maintenance runs a test for defective pixels and should
 * be triggered periodically by the client. Internally, a defective
 * pixel list is maintained and the algorithm will compensate for
 * any defect pixels when matching towards a template.
 *
 * The defective pixel update will abort and return an error if any of
 * the finger detect zones are covered. A client can call
 * fp_sensor_finger_status to determine the current status.
 *
 * @param[in]  image_data      pointer to a buffer containing at least
 * FP_SENSOR_IMAGE_SIZE_EGIS bytes of memory
 * @param[out] fp_sensor_info  Structure containing output data.
 *
 * @return
 * - 0 on success
 * - negative value on error
 */
int fp_sensor_maintenance(uint8_t *image_data,
			  struct egis660_fp_sensor_info *fp_sensor_info);

/** Image captured. */
#define EGIS_SENSOR_GOOD_IMAGE_QUALITY 0
/** Image captured but quality is too low. */
#define EGIS_SENSOR_LOW_IMAGE_QUALITY 1
/** Finger removed before image was captured. */
#define EGIS_SENSOR_TOO_FAST 2
/** Sensor not fully covered by finger. */
#define EGIS_SENSOR_LOW_COVERAGE 3

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
 * Configure finger detection.
 *
 * Send the settings to the sensor, so it is properly configured to detect
 * the presence of a finger.
 */
void fp_sensor_configure_detect(void);

#define EGIS_FINGER_NONE 0
#define EGIS_FINGER_PARTIAL 1
#define EGIS_FINGER_PRESENT 2

/**
 * Returns the status of the finger on the sensor.
 * (assumes fp_sensor_configure_detect was called before)
 *
 * @return finger_state
 */
int fp_sensor_finger_status(void);

/**
 * Lock the FP sensor before SPI transaction.
 *
 * The function performs all needed operations before SPI transaction,
 * it may include locking, power actions etc. The function is blocking,
 * if the sensor is already locked.
 *
 * The function can be called by a thread multiple times. fp_sensor_unlock has
 * to be called only once to unlock the access.
 *
 * @param dev  Pointer to FP sensor device
 */
void fp_sensor_lock(const struct device *dev);

/**
 * Unlock the FP sensor after SPI transaction.
 *
 * Unlock access to the FP sensor.
 *
 * @param dev  Pointer to FP sensor device
 */
void fp_sensor_unlock(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_EGIS660_PRIVATE_H_ */
