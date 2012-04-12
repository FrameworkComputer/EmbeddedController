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

/**
 * Get the scan data from the keyboard.
 *
 * This returns the results of the last keyboard scan, by pointing the
 * supplied buffer to it, and returning the number of bytes available.
 *
 * The supplied buffer can be used directly if required, but in that case
 * the number of bytes available is limited to 'max_bytes'.
 *
 * @param buffp		Pointer to buffer to contain data
 * @param max_bytes	Maximum number of bytes available in *buffp
 * @return number of bytes available, or -1 for error
 */
int keyboard_get_scan(uint8_t **buffp, int max_bytes);

#endif  /* __CROS_KEYBOARD_SCAN_H */
