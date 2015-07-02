/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lid angle module for Chrome EC */

#ifndef __CROS_EC_LID_ANGLE_H
#define __CROS_EC_LID_ANGLE_H

/**
 * Update the lid angle module with the most recent lid angle calculation. Then
 * use the lid angle history to enable/disable peripheral devices, keyboard
 * scanning and track pad interrupt, etc.
 *
 * @param lid_ang Lid angle.
 */
void lid_angle_update(int lid_ang);

/**
 * Getter and setter methods for the keyboard wake angle. In S3, when the
 * lid angle is greater than this value, the peripheral devices are disabled,
 * and when the lid angle is smaller than this value, they are enabled.
 */
int lid_angle_get_wake_angle(void);
void lid_angle_set_wake_angle(int ang);

/**
 * Board level callback for lid angle changes.
 *
 * @param enable Flag that enables or disables peripherals.
 */
void lid_angle_peripheral_enable(int enable);

#endif  /* __CROS_EC_LID_ANGLE_H */
