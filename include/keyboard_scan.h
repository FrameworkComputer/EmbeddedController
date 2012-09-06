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

/* Key held down at keyboard-controlled reset boot time. */
enum boot_key {
	BOOT_KEY_NONE,  /* No keys other than keyboard-controlled reset keys */
	BOOT_KEY_ESC,
	BOOT_KEY_DOWN_ARROW,
	BOOT_KEY_OTHER = -1,  /* None of the above */
};

/*
 * Return the key held down at boot time in addition to the keyboard-controlled
 * reset keys.  Returns BOOT_KEY_OTHER if none of the keys specifically checked
 * was pressed, or reset was not caused by a keyboard-controlled reset, or if
 * the state has been cleared by keyboard_scan_clear_boot_key().
 */
enum boot_key keyboard_scan_get_boot_key(void);

/* Return non-zero if recovery key was pressed at boot. */
int keyboard_scan_recovery_pressed(void);

/* Clear any saved keyboard state (empty FIFO, etc) */
void keyboard_clear_state(void);

/* Enables/disables keyboard matrix scan. */
void keyboard_enable_scanning(int enable);

#endif  /* __CROS_EC_KEYBOARD_SCAN_H */
