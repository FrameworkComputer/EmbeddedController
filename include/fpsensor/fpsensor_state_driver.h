/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_STATE_DRIVER_INFO_H
#define __CROS_EC_FPSENSOR_FPSENSOR_STATE_DRIVER_INFO_H

#include "fpsensor_driver.h"

#include <stdint.h>

/* --- Global variables defined in fpsensor_state.c --- */

/* Last acquired frame (aligned as it is used by arbitrary binary libraries) */
extern uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE];

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_STATE_DRIVER_INFO_H */
