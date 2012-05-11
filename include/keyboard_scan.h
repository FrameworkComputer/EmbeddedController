/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#ifndef __CROS_EC_KEYBOARD_SCAN_H
#define __CROS_EC_KEYBOARD_SCAN_H

#include "common.h"

/* Initializes the module. */
int keyboard_scan_init(void);

/* Returns non-zero if recovery key was pressed at boot. */
int keyboard_scan_recovery_pressed(void);

/* clear any saved keyboard state (empty FIFO, etc) */
void keyboard_clear_state(void);

#endif  /* __CROS_KEYBOARD_SCAN_H */
