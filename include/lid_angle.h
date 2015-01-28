/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lid angle module for Chrome EC */

#ifndef __CROS_EC_LID_ANGLE_H
#define __CROS_EC_LID_ANGLE_H

/**
 * Update the lid angle module with the most recent lid angle calculation. Then
 * use the lid angle history to enable/disable keyboard scanning when chipset
 * is suspended.
 *
 * @lid_ang Lid angle.
 */
void lidangle_keyscan_update(int lid_ang);

/**
 * Getter and setter methods for the keyboard wake angle. In S3, when the
 * lid angle is greater than this value, the keyboard is disabled, and when
 * the lid angle is smaller than this value, the keyboard is enabled.
 */
int lid_angle_get_kb_wake_angle(void);
void lid_angle_set_kb_wake_angle(int ang);

#endif  /* __CROS_EC_LID_ANGLE_H */
