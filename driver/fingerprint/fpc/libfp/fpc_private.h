/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Private sensor interface */

#ifndef __CROS_EC_FPC_PRIVATE_H
#define __CROS_EC_FPC_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

/* External error codes from FPC's sensor library */
enum fpc_error_code_external {
	FPC_ERROR_NONE = 0,
	FPC_ERROR_NOT_FOUND = 1,
	FPC_ERROR_CAN_BE_USED_2 = 2,
	FPC_ERROR_CAN_BE_USED_3 = 3,
	FPC_ERROR_CAN_BE_USED_4 = 4,
	FPC_ERROR_PAL = 5,
	FPC_ERROR_IO = 6,
	FPC_ERROR_CANCELLED = 7,
	FPC_ERROR_UNKNOWN = 8,
	FPC_ERROR_MEMORY = 9,
	FPC_ERROR_PARAMETER = 10,
	FPC_ERROR_TEST_FAILED = 11,
	FPC_ERROR_TIMEDOUT = 12,
	FPC_ERROR_SENSOR = 13,
	FPC_ERROR_SPI = 14,
	FPC_ERROR_NOT_SUPPORTED = 15,
	FPC_ERROR_OTP = 16,
	FPC_ERROR_STATE = 17,
	FPC_ERROR_PN = 18,
	FPC_ERROR_DEAD_PIXELS = 19,
	FPC_ERROR_TEMPLATE_CORRUPTED = 20,
	FPC_ERROR_CRC = 21,
	FPC_ERROR_STORAGE = 22, /**< Errors related to storage **/
	FPC_ERROR_MAXIMUM_REACHED = 23, /**< The allowed maximum has been
					   reached       **/
	FPC_ERROR_MINIMUM_NOT_REACHED = 24, /**< The required minimum was not
					       reached       **/
	FPC_ERROR_SENSOR_LOW_COVERAGE = 25, /**< Minimum sensor coverage was not
					       reached    **/
	FPC_ERROR_SENSOR_LOW_QUALITY = 26, /**< Sensor image is considered low
					      quality     **/
	FPC_ERROR_SENSOR_FINGER_NOT_STABLE = 27, /**< Finger was not stable
						    during image capture **/
};

/* Internal error codes from FPC's sensor library */
enum fpc_error_code_internal {
	FPC_ERROR_INTERNAL_0 = 0, /* Indicates that no internal code was set. */
	FPC_ERROR_INTERNAL_1 = 1, /* Not supported by sensor. */
	FPC_ERROR_INTERNAL_2 = 2, /* Sensor got a NULL response (from other
				     module).                  */
	FPC_ERROR_INTERNAL_3 = 3, /* Runtime config not supported by firmware.
				   */
	FPC_ERROR_INTERNAL_4 = 4, /* CAC has not been created. */
	FPC_ERROR_INTERNAL_5 = 5, /* CAC returned an error to the sensor. */
	FPC_ERROR_INTERNAL_6 = 6, /* CAC fasttap image capture failed. */
	FPC_ERROR_INTERNAL_7 = 7, /* CAC fasttap image capture failed. */
	FPC_ERROR_INTERNAL_8 = 8, /* CAC Simple image capture failed. */
	FPC_ERROR_INTERNAL_9 = 9, /* CAC custom image capture failed. */
	FPC_ERROR_INTERNAL_10 = 10, /* CAC MQT image capture failed. */
	FPC_ERROR_INTERNAL_11 = 11, /* CAC PN image capture failed. */
	FPC_ERROR_INTERNAL_12 = 12, /* Reading CAC context size. */
	FPC_ERROR_INTERNAL_13 = 13, /* Reading CAC context size. */
	FPC_ERROR_INTERNAL_14 = 14, /* Sensor context invalid. */
	FPC_ERROR_INTERNAL_15 = 15, /* Buffer reference is invalid. */
	FPC_ERROR_INTERNAL_16 = 16, /* Buffer size reference is invalid. */
	FPC_ERROR_INTERNAL_17 = 17, /* Image data reference is invalid. */
	FPC_ERROR_INTERNAL_18 = 18, /* Capture type specified is invalid. */
	FPC_ERROR_INTERNAL_19 = 19, /* Capture config specified is invalid. */
	FPC_ERROR_INTERNAL_20 = 20, /* Sensor type in hw desc could not be
				       extracted.                   */
	FPC_ERROR_INTERNAL_21 = 21, /* Failed to create BNC component. */
	FPC_ERROR_INTERNAL_22 = 22, /* BN calibration failed. */
	FPC_ERROR_INTERNAL_23 = 23, /* BN memory allocation failed. */
	FPC_ERROR_INTERNAL_24 = 24, /* Companion type in hw desc could not be
				       extracted.                */
	FPC_ERROR_INTERNAL_25 = 25, /* Coating type in hw desc could not be
				       extracted.                  */
	FPC_ERROR_INTERNAL_26 = 26, /* Sensor mode type is invalid. */
	FPC_ERROR_INTERNAL_27 = 27, /* Wrong Sensor state in OTP read. */
	FPC_ERROR_INTERNAL_28 = 28, /* Mismatch of register size in overlay vs
				       rrs.                     */
	FPC_ERROR_INTERNAL_29 = 29, /* Checkerboard capture failed. */
	FPC_ERROR_INTERNAL_30 = 30, /* Error converting to fpc_image in dp
				       calibration.                 */
	FPC_ERROR_INTERNAL_31 = 31, /* Failed to capture reset pixel image. */
	FPC_ERROR_INTERNAL_32 = 32, /* API level not support in dp calibration.
				     */
	FPC_ERROR_INTERNAL_33 = 33, /* The image data in parameter is invalid.
				     */
	FPC_ERROR_INTERNAL_34 = 34, /* PAL delay function has failed. */
	FPC_ERROR_INTERNAL_35 = 35, /* AFD sensor commad did not complete. */
	FPC_ERROR_INTERNAL_36 = 36, /* AFD wrong runlevel detected after
				       calibration.                   */
	FPC_ERROR_INTERNAL_37 = 37, /* Wrong rrs size. */
	FPC_ERROR_INTERNAL_38 = 38, /* There was a finger on the sensor when
				       calibrating finger detect. */
	FPC_ERROR_INTERNAL_39 = 39, /* The calculated calibration value is
				       larger than max.             */
	FPC_ERROR_INTERNAL_40 = 40, /* The sensor fifo always underflows */
	FPC_ERROR_INTERNAL_41 = 41, /* The oscillator calibration resulted in a
				       too high or low value   */
	FPC_ERROR_INTERNAL_42 = 42, /* Sensor driver was opened with NULL
				       configuration                 */
	FPC_ERROR_INTERNAL_43 = 43, /* Sensor driver as opened with NULL hw
				       descriptor                  */
	FPC_ERROR_INTERNAL_44 = 44, /* Error occured during image drive test */
};

/* FPC specific initialization function to fill their context */
__staticlib int fp_sensor_open(void *ctx, uint32_t ctx_size);

/*
 * Get library version code.
 * version code contains three digits. x.y.z
 *   x - major version
 *   y - minor version
 *   z - build index
 */
__staticlib const char *fp_sensor_get_version(void);

typedef struct {
	uint32_t num_defective_pixels;
} fp_sensor_info_t;

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
 * @param[in]  image_data      pointer to FP_SENSOR_IMAGE_SIZE bytes of memory
 * @param[out] fp_sensor_info  Structure containing output data.
 *
 * @return
 * - 0 on success
 * - negative value on error
 */
__staticlib int fp_sensor_maintenance(uint8_t *image_data,
				      fp_sensor_info_t *fp_sensor_info);

/**
 * Get the HWID of the sensor.
 *
 * @param id Pointer to where to store the HWID value.  The HWID value here is
 * the full 16 bits (contrast to FP_SENSOR_HWID where the lower four bits, which
 * are a manufacturing id, are truncated).
 * @return
 * - EC_SUCCESS on success
 * - EC_ERROR_INVAL or EC_ERROR_HW_INTERNAL on error
 */
int fpc_get_hwid(uint16_t *id);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FPC_PRIVATE_H */
