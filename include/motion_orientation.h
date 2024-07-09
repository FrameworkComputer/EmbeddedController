/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* header for an orientation sensor. */
#ifndef __CROS_EC_MOTION_ORIENTATION_H
#define __CROS_EC_MOTION_ORIENTATION_H

#include "chipset.h"
#include "common.h"
#include "ec_commands.h"
#include "motion_sense.h"

#ifdef __cplusplus
extern "C" {
#endif

enum motionsensor_orientation
motion_orientation_remap(const struct motion_sensor_t *s,
			 enum motionsensor_orientation orientation);

bool motion_orientation_changed(const struct motion_sensor_t *s);
enum motionsensor_orientation *
motion_orientation_ptr(const struct motion_sensor_t *s);
void motion_orientation_update(const struct motion_sensor_t *s);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_MOTION_ORIENTATION_H */
