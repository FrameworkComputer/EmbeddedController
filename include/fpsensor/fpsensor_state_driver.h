/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_STATE_DRIVER_INFO_H
#define __CROS_EC_FPSENSOR_FPSENSOR_STATE_DRIVER_INFO_H

#include "fpsensor_driver.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Global variables defined in fpsensor_state.c --- */

/* Last acquired frame (aligned as it is used by arbitrary binary libraries) */
extern uint8_t fp_buffer[FP_SENSOR_IMAGE_SIZE];

/* Events for the FPSENSOR task */
#define TASK_EVENT_SENSOR_IRQ TASK_EVENT_CUSTOM_BIT(0)
#define TASK_EVENT_UPDATE_CONFIG TASK_EVENT_CUSTOM_BIT(1)

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_STATE_DRIVER_INFO_H */
