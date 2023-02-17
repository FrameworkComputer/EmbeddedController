/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPC1025_PRIVATE_H
#define __CROS_EC_FPC1025_PRIVATE_H

/* The 16-bit hardware ID is 0x021y */
#define FP_SENSOR_HWID_FPC 0x021

/* Sensor type name */
#define FP_SENSOR_NAME_FPC "FPC1025"

/* Sensor pixel resolution */
#define FP_SENSOR_RES_X_FPC (160) /**< Sensor width */
#define FP_SENSOR_RES_Y_FPC (160) /**< Sensor height */
#define FP_SENSOR_RES_BPP_FPC (8) /**< Resolution bits per pixel */

/*
 * Sensor image size
 *
 * Value from fpc_bep_image_get_buffer_size(): (160*160)+660
 */
#define FP_SENSOR_IMAGE_SIZE_FPC (26260)
#define FP_SENSOR_REAL_IMAGE_SIZE_FPC \
	(FP_SENSOR_RES_X_FPC * FP_SENSOR_RES_Y_FPC)
/* Offset of image data in fp_buffer */
#define FP_SENSOR_IMAGE_OFFSET_FPC (400)

/*
 * Constant value for the enrollment data size
 *
 * Size of private fp_bio_enrollment_t
 */
#define FP_ALGORITHM_ENROLLMENT_SIZE_FPC (4)

/*
 * Constant value corresponding to the maximum template size
 * for FPC1025 sensor. Client template memory allocation must
 * have this size. This includes extra memory for template update.
 *
 * Template size + alignment padding + size of template size variable
 */
#define FP_ALGORITHM_TEMPLATE_SIZE_FPC (5088 + 0 + 4)

/* Max number of templates stored / matched against */
#ifndef FP_MAX_FINGER_COUNT_FPC
#define FP_MAX_FINGER_COUNT_FPC (5)
#endif

#endif /* __CROS_EC_FPC1025_PRIVATE_H */
