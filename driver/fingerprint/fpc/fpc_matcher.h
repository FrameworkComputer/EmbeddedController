/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_FPC_FPC_MATCHER_H_
#define __CROS_EC_DRIVER_FINGERPRINT_FPC_FPC_MATCHER_H_

#include "common.h"

#if defined(CONFIG_FP_SENSOR_FPC1025) || defined(CONFIG_FP_SENSOR_FPC1035)
#include "bep/fpc_bep_matcher.h"
#elif defined(CONFIG_FP_SENSOR_FPC1145)
#include "libfp/fpc_libfp_matcher.h"
#else
#error "Sensor type not defined!"
#endif

#endif /* __CROS_EC_DRIVER_FINGERPRINT_FPC_FPC_MATCHER_H_ */
