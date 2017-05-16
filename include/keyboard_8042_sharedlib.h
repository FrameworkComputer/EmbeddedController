/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The functions implemented by keyboard component of EC core.
 */

#ifndef __CROS_EC_KEYBOARD_8042_SHAREDLIB_H
#define __CROS_EC_KEYBOARD_8042_SHAREDLIB_H

#include "button.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"

struct button_8042_t {
	uint16_t scancode_set1;
	uint16_t scancode_set2;
	int repeat;
};

/* The standard Chrome OS keyboard matrix table. */
#ifdef CONFIG_KEYBOARD_SCANCODE_MUTABLE
extern uint16_t scancode_set1[KEYBOARD_ROWS][KEYBOARD_COLS];
extern uint16_t scancode_set2[KEYBOARD_ROWS][KEYBOARD_COLS];
#else
extern const uint16_t scancode_set1[KEYBOARD_ROWS][KEYBOARD_COLS];
extern const uint16_t scancode_set2[KEYBOARD_ROWS][KEYBOARD_COLS];
#endif

/* Button scancodes (Power, Volume Down, Volume Up, etc.) */
extern const struct button_8042_t buttons_8042[KEYBOARD_BUTTON_COUNT];

#endif /* __CROS_EC_KEYBOARD_8042_SHAREDLIB_H */
