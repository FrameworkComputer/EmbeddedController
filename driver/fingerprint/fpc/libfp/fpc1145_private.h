/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPC1145_PRIVATE_H
#define __CROS_EC_FPC1145_PRIVATE_H

#include <stdint.h>

/**
 * The hardware ID is 16-bits. All 114x FPC sensors (including FPC1145) are
 * detected with the pattern 0x1400 and mask 0xFFF0. All supported variants of
 * the 1145 (0x140B, 0x140C, and 0x1401) should be detected as part of the FPC
 * 1140 family with identical functionality.
 * See http://b/150407388 for additional details.
 */
#define FP_SENSOR_HWID 0x140

/* Sensor type name */
#define FP_SENSOR_NAME "FPC1145"

/* Sensor pixel resolution */
#define FP_SENSOR_RES_Y 192
#define FP_SENSOR_RES_X 56
#define FP_SENSOR_RES_BPP 8

/* Acquired finger frame definitions */
#define FP_SENSOR_IMAGE_SIZE_MODE_VENDOR (35460)
#define FP_SENSOR_IMAGE_SIZE_MODE_SIMPLE (13356)
/*
 * Size of the captured image in MQT mode. If you this is modified the
 * corresponding value in the MQT tool fputils.py must be changed too.
 * See b/111443750 for context.
 */
#define FP_SENSOR_IMAGE_SIZE_MODE_QUAL (24408)

#define FP_SENSOR_IMAGE_SIZE FP_SENSOR_IMAGE_SIZE_MODE_VENDOR
#define FP_SENSOR_IMAGE_OFFSET 2340

/* Opaque FPC context */
#define FP_SENSOR_CONTEXT_SIZE 4944

/* Algorithm buffer sizes */
#define FP_ALGORITHM_ENROLLMENT_SIZE 28
#define FP_ALGORITHM_TEMPLATE_SIZE 47552

/* Max number of templates stored / matched against */
#define FP_MAX_FINGER_COUNT 5

#endif /* __CROS_EC_FPC1145_PRIVATE_H */
