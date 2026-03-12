/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_SHIM_INCLUDE_FPSENSOR_DRIVER_H__
#define __ZEPHYR_SHIM_INCLUDE_FPSENSOR_DRIVER_H__

#include <drivers/fingerprint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_FINGERPRINT_SENSOR_IMAGE_OFFSET
#define FP_SENSOR_IMAGE_OFFSET CONFIG_FINGERPRINT_SENSOR_IMAGE_OFFSET
#else
#define FP_SENSOR_IMAGE_OFFSET IMAGE_OFFSET
#endif

#ifdef CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE
#define FP_SENSOR_IMAGE_SIZE CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE
#else
#define FP_SENSOR_IMAGE_SIZE \
	MAX_FRAME_SIZE(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

/*
 * Tell fpsensor code that private driver is present, even if this is a public
 * build. If the build is public, we will provide mocks.
 */
#define HAVE_FP_PRIVATE_DRIVER

#ifdef __cplusplus
}
#endif

#endif /* __ZEPHYR_SHIM_INCLUDE_FPSENSOR_DRIVER_H__ */
