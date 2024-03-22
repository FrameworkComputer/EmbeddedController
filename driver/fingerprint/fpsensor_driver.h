/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_DRIVER_H_
#define __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_DRIVER_H_

#if defined(HAVE_PRIVATE) && !defined(EMU_BUILD)
#define HAVE_FP_PRIVATE_DRIVER
#if defined(CONFIG_FP_SENSOR_ELAN80) || defined(CONFIG_FP_SENSOR_ELAN515) || \
	defined(CONFIG_FP_SENSOR_ELAN80SG)
#include "elan/elan_sensor.h"
#define FP_SENSOR_IMAGE_OFFSET (FP_SENSOR_IMAGE_OFFSET_ELAN)
#define FP_SENSOR_IMAGE_SIZE (FP_SENSOR_IMAGE_SIZE_ELAN)
#define FP_SENSOR_RES_X (FP_SENSOR_RES_X_ELAN)
#define FP_SENSOR_RES_Y (FP_SENSOR_RES_Y_ELAN)
#endif /* CONFIG_FP_SENSOR_ELAN80 || CONFIG_FP_SENSOR_ELAN515 */

#if defined(CONFIG_FP_SENSOR_FPC1025) || defined(CONFIG_FP_SENSOR_FPC1145)
#include "fpc/fpc_sensor.h"
#define FP_SENSOR_IMAGE_OFFSET (FP_SENSOR_IMAGE_OFFSET_FPC)
#define FP_SENSOR_IMAGE_SIZE (FP_SENSOR_IMAGE_SIZE_FPC)
#define FP_SENSOR_RES_X (FP_SENSOR_RES_X_FPC)
#define FP_SENSOR_RES_Y (FP_SENSOR_RES_Y_FPC)
#endif /* CONFIG_FP_SENSOR_FPC1025 || CONFIG_FP_SENSOR_FPC1145 */

#endif /* HAVE_PRIVATE && !EMU_BUILD */

/* These values are used for public or host (emulator) tests. */
#if !defined(HAVE_PRIVATE) || defined(EMU_BUILD)
#define FP_SENSOR_IMAGE_OFFSET (0)
#define FP_SENSOR_IMAGE_SIZE 0
#define FP_SENSOR_RES_X 0
#define FP_SENSOR_RES_Y 0
#endif /* !HAVE_PRIVATE || EMU_BUILD */

#if defined(HAVE_PRIVATE) && defined(TEST_BUILD)
/*
 * For unittest in a private build, enable driver-related code in
 * common/fpsensor/ so that they can be tested (with fpsensor_mock).
 */
#define HAVE_FP_PRIVATE_DRIVER
#endif

#if !defined(FP_SENSOR_IMAGE_OFFSET) && defined(TEST_BUILD)
#define FP_SENSOR_IMAGE_OFFSET (0)
#endif

#endif /* __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_DRIVER_H_ */
