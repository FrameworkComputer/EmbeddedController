/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __cplusplus
#error This header requires C++ to use.
#endif

#ifndef __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_MATCHER_H_
#define __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_MATCHER_H_

#include <stdint.h>

#if defined(HAVE_PRIVATE) && !defined(EMU_BUILD)

#if defined(CONFIG_FP_SENSOR_ELAN80) || defined(CONFIG_FP_SENSOR_ELAN515) || \
	defined(CONFIG_FP_SENSOR_ELAN80SG)
#include "elan/elan_matcher.h"
#define FP_ALGORITHM_TEMPLATE_SIZE (FP_ALGORITHM_TEMPLATE_SIZE_ELAN)
#define FP_MAX_FINGER_COUNT (FP_MAX_FINGER_COUNT_ELAN)
#endif /* FP_SENSOR_ELAN80 || SENSOR_ELAN515 || FP_SENSOR_ELAN80SG */

#if defined(CONFIG_FP_SENSOR_FPC1025) || defined(CONFIG_FP_SENSOR_FPC1035) || \
	defined(CONFIG_FP_SENSOR_FPC1145)
#include "fpc/fpc_matcher.h"
#define FP_ALGORITHM_TEMPLATE_SIZE (FP_ALGORITHM_TEMPLATE_SIZE_FPC)
#define FP_MAX_FINGER_COUNT (FP_MAX_FINGER_COUNT_FPC)
#endif /* FP_SENSOR_FPC1025 || FP_SENSOR_FPC1035 || FP_SENSOR_FPC1145 */

#endif /* HAVE_PRIVATE && !EMU_BUILD */

/* These values are used for public or host (emulator) tests. */
#if !defined(HAVE_PRIVATE) || defined(EMU_BUILD)
#define FP_ALGORITHM_TEMPLATE_SIZE 4
#define FP_MAX_FINGER_COUNT 5
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

/*
 * Druid can be used in EMU/host environments, since it can be compiled for any
 * target platform and, thus, does not have the same restrictions as the
 * above private matching libraries.
 */
#if defined(CONFIG_LIB_DRUID_WRAPPER) && defined(HAVE_PRIVATE)

#undef FP_ALGORITHM_TEMPLATE_SIZE
#undef FP_MAX_FINGER_COUNT
#include "mcu/cros/template_storage.h"

#endif /* CONFIG_LIB_DRUID_WRAPPER && HAVE_PRIVATE */

#endif /* __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_MATCHER_H_ */
