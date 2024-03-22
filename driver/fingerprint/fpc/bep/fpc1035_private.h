/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPC1035_PRIVATE_H
#define __CROS_EC_FPC1035_PRIVATE_H

/* The 16-bit hardware ID is 0x011y */
#define FP_SENSOR_HWID_FPC 0x011

/* Sensor type name */
#define FP_SENSOR_NAME_FPC "FPC1035"

/* Sensor pixel resolution */
#define FP_SENSOR_RES_X_FPC (112) /**< Sensor width */
#define FP_SENSOR_RES_Y_FPC (88) /**< Sensor height */
#define FP_SENSOR_RES_BPP_FPC (8) /**< Resolution bits per pixel */

/*
 * Sensor image size
 *
 * Value from fpc_bep_image_get_buffer_size(): (112*88)+660
 */
#define FP_SENSOR_IMAGE_SIZE_FPC (10516)
#define FP_SENSOR_REAL_IMAGE_SIZE_FPC \
	(FP_SENSOR_RES_X_FPC * FP_SENSOR_RES_Y_FPC)
/* Offset of image data in fp_buffer */
#define FP_SENSOR_IMAGE_OFFSET_FPC (400)

#endif /* __CROS_EC_FPC1035_PRIVATE_H */
