/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_FPC_FPC_SENSOR_H_
#define __CROS_EC_DRIVER_FINGERPRINT_FPC_FPC_SENSOR_H_

#include "common.h"

#if defined(CONFIG_FP_SENSOR_FPC1025)
#include "bep/fpc1025_private.h"
#elif defined(CONFIG_FP_SENSOR_FPC1035)
#include "bep/fpc1035_private.h"
#elif defined(CONFIG_FP_SENSOR_FPC1145)
#include "libfp/fpc1145_private.h"
#else
#error "Sensor type not defined!"
#endif

/**
 * Runs a test for defective pixels.
 *
 * Should be triggered periodically by the client. The maintenance command can
 * take several hundred milliseconds to run.
 *
 * @return EC_ERROR_HW_INTERNAL on error (such as finger on sensor)
 * @return EC_SUCCESS on success
 */
int fpc_fp_maintenance(uint16_t *error_state);

#endif /* __CROS_EC_DRIVER_FINGERPRINT_FPC_FPC_SENSOR_H_ */
