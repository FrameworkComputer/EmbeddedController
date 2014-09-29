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

/**
 * Gesture hook to call on chipset resume.
 */
void gesture_chipset_resume(void);

/**
 * Gesture hook to call on chipset suspend.
 */
void gesture_chipset_suspend(void);

/**
 * Gesture hook to call on chipset shutdown.
 */
void gesture_chipset_shutdown(void);

#endif /* __CROS_EC_GESTURE_H */
