/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for gesture.c */

#ifndef __CROS_EC_GESTURE_H
#define __CROS_EC_GESTURE_H

/**
 * Run gesture detection engine.
 */
void gesture_calc(void);

/* gesture hooks are triggered after the motion sense hooks. */
#define GESTURE_HOOK_PRIO (MOTION_SENSE_HOOK_PRIO + 10)

#endif /* __CROS_EC_GESTURE_H */
