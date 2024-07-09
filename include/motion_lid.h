/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for motion_lid.h */

#ifndef __CROS_EC_MOTION_LID_H
#define __CROS_EC_MOTION_LID_H

#include "host_command.h"
#include "math_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * We will change our tablet mode status when we are "convinced" that it has
 * changed.  This means we will have to consecutively calculate our new tablet
 * mode while the angle is stable and come to the same conclusion.  The number
 * of consecutive calculations is the debounce count with an interval between
 * readings set by the motion_sense task.  This should avoid spurious forces
 * that may trigger false transitions of the tablet mode switch.
 */
#define TABLET_MODE_DEBOUNCE_COUNT 3

/**
 * Get last calculated lid angle. Note, the lid angle calculated by the EC
 * is un-calibrated and is an approximate angle.
 *
 * @return lid angle in degrees in range [0, 360], or LID_ANGLE_UNRELIABLE
 * if the lid angle can't be determined.
 */
int motion_lid_get_angle(void);

enum ec_status host_cmd_motion_lid(struct host_cmd_handler_args *args);

void motion_lid_calc(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_MOTION_LID_H */
