/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Private sensor interface */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_FPC1025_PRIVATE_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_FPC1025_PRIVATE_H_

#include <stdint.h>

/* FPC type which keeps sensor specific information. */
struct fpc_bep_sensor;

/* Declare FPC1025 sensor specific information. */
extern const struct fpc_bep_sensor fpc_bep_sensor_1025;

struct fpc_sensor_info {
	const struct fpc_bep_sensor *sensor;
	uint32_t image_buffer_size;
};

/** @brief Common results returned by BEP functions.
 *
 * BEP config/usage errors:
 * Examples: Incorrect arguments/parameters when calling BEP API
 * functions; functions called in incorrect order.
 * Action: Fix SW bug.
 * FPC_BEP_RESULT_GENERAL_ERROR
 * FPC_BEP_RESULT_NOT_IMPLEMENTED
 * FPC_BEP_RESULT_NOT_SUPPORTED
 * FPC_BEP_RESULT_NOT_INITIALIZED
 * FPC_BEP_RESULT_CANCELLED
 * FPC_BEP_RESULT_NO_RESOURCE
 * FPC_BEP_RESULT_WRONG_STATE
 * FPC_BEP_RESULT_ID_NOT_UNIQUE
 * FPC_BEP_RESULT_ID_NOT_FOUND
 * FPC_BEP_RESULT_INVALID_FORMAT
 * FPC_BEP_RESULT_INVALID_ARGUMENT
 * FPC_BEP_RESULT_INVALID_PARAMETER
 * FPC_BEP_RESULT_INVALID_CALIBRATION
 * FPC_BEP_RESULT_MISSING_TEMPLATE
 * FPC_BEP_RESULT_STORAGE_NOT_FORMATTED
 * FPC_BEP_RESULT_SENSOR_NOT_INITIALIZED
 * FPC_BEP_RESULT_SENSOR_MISMATCH
 * FPC_BEP_RESULT_CRYPTO_ERROR
 *
 * Dynamic memory/heap errors:
 * Examples: Memory leak; heap is too small.
 * Action: Fix SW bug.
 * FPC_BEP_RESULT_NO_MEMORY
 *
 * Sensor and communication errors:
 * Examples: Broken sensor communication lines; unstable power supply.
 * Action: Fix HW bug.
 * FPC_BEP_RESULT_BROKEN_SENSOR
 * FPC_BEP_RESULT_INTERNAL_ERROR
 * FPC_BEP_RESULT_TIMEOUT
 * FPC_BEP_RESULT_IO_ERROR
 *
 * Image capture errors:
 * Examples: Finger removed from sensor too quickly.
 * Action: Call the function again.
 * FPC_BEP_RESULT_IMAGE_CAPTURE_ERROR
 */

enum fpc_bep_result {
	/** No errors occurred. */
	FPC_BEP_RESULT_OK = 0,
	/** General error. */
	FPC_BEP_RESULT_GENERAL_ERROR = -1,
	/** Internal error. */
	FPC_BEP_RESULT_INTERNAL_ERROR = -2,
	/** Invalid argument. */
	FPC_BEP_RESULT_INVALID_ARGUMENT = -3,
	/** The functionality is not implemented. */
	FPC_BEP_RESULT_NOT_IMPLEMENTED = -4,
	/** The operation was cancelled. */
	FPC_BEP_RESULT_CANCELLED = -5,
	/** Out of memory. */
	FPC_BEP_RESULT_NO_MEMORY = -6,
	/** Resources are not available. */
	FPC_BEP_RESULT_NO_RESOURCE = -7,
	/** An I/O error occurred. */
	FPC_BEP_RESULT_IO_ERROR = -8,
	/** Sensor is broken. */
	FPC_BEP_RESULT_BROKEN_SENSOR = -9,
	/** The operation cannot be performed in the current state. */
	FPC_BEP_RESULT_WRONG_STATE = -10,
	/** The operation timed out. */
	FPC_BEP_RESULT_TIMEOUT = -11,
	/** The ID is not unique. */
	FPC_BEP_RESULT_ID_NOT_UNIQUE = -12,
	/** The ID is not found. */
	FPC_BEP_RESULT_ID_NOT_FOUND = -13,
	/** The format is invalid. */
	FPC_BEP_RESULT_INVALID_FORMAT = -14,
	/** An image capture error occurred. */
	FPC_BEP_RESULT_IMAGE_CAPTURE_ERROR = -15,
	/** Sensor hardware id or sensor configuration mismatch. */
	FPC_BEP_RESULT_SENSOR_MISMATCH = -16,
	/** Invalid parameter. */
	FPC_BEP_RESULT_INVALID_PARAMETER = -17,
	/** Missing Template. */
	FPC_BEP_RESULT_MISSING_TEMPLATE = -18,
	/** Invalid Calibration.*/
	FPC_BEP_RESULT_INVALID_CALIBRATION = -19,
	/** Calibration/template storage not formatted.*/
	FPC_BEP_RESULT_STORAGE_NOT_FORMATTED = -20,
	/** Sensor hasn't been initialized. */
	FPC_BEP_RESULT_SENSOR_NOT_INITIALIZED = -21,
	/** Enroll fail after too many bad images. */
	FPC_BEP_RESULT_TOO_MANY_BAD_IMAGES = -22,
	/** Cryptographic operation failed. */
	FPC_BEP_RESULT_CRYPTO_ERROR = -23,
	/** The functionality is not supported. */
	FPC_BEP_RESULT_NOT_SUPPORTED = -24,
	/** Finger not stable. */
	FPC_BEP_RESULT_FINGER_NOT_STABLE = -25,
	/** The functionality could not be used before it's initialized. */
	FPC_BEP_RESULT_NOT_INITIALIZED = -26,
};

/* FPC specific initialization and de-initialization functions */
int fp_sensor_open(void);
int fp_sensor_close(void);

/* Get FPC library version code.*/
const char *fp_sensor_get_version(void);

/* Get FPC library build info.*/
const char *fp_sensor_get_build_info(void);

struct fp_sensor_info {
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
 * FP_SENSOR_IMAGE_SIZE_FPC bytes of memory
 * @param[out] fp_sensor_info  Structure containing output data.
 *
 * @return
 * - 0 on success
 * - negative value on error
 */
int fp_sensor_maintenance(uint8_t *image_data,
			  struct fp_sensor_info *fp_sensor_info);

/** Image captured. */
#define FPC_SENSOR_GOOD_IMAGE_QUALITY 0
/** Image captured but quality is too low. */
#define FPC_SENSOR_LOW_IMAGE_QUALITY 1
/** Finger removed before image was captured. */
#define FPC_SENSOR_TOO_FAST 2
/** Sensor not fully covered by finger. */
#define FPC_SENSOR_LOW_COVERAGE 3

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

#define FPC_FINGER_NONE 0
#define FPC_FINGER_PARTIAL 1
#define FPC_FINGER_PRESENT 2

/**
 * Returns the status of the finger on the sensor.
 * (assumes fp_sensor_configure_detect was called before)
 *
 * @return finger_state
 */
int fp_sensor_finger_status(void);

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_FPC1025_PRIVATE_H_ */
