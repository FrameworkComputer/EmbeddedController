/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard configuration constants for Chrome EC */

#ifndef __CROS_EC_KEYBOARD_CONFIG_H
#define __CROS_EC_KEYBOARD_CONFIG_H

#include "common.h"

/* Keyboard matrix is 13 output columns x 8 input rows */
#define KEYBOARD_COLS 13
#define KEYBOARD_ROWS 8

/* Columns and masks for keys we particularly care about */
#define KEYBOARD_COL_DOWN       11
#define KEYBOARD_MASK_DOWN      0x40
#define KEYBOARD_COL_ESC	1
#define KEYBOARD_MASK_ESC	0x02
#define KEYBOARD_COL_KEY_H	6
#define KEYBOARD_MASK_KEY_H	0x02
#define KEYBOARD_COL_KEY_R	3
#define KEYBOARD_MASK_KEY_R	0x80
#define KEYBOARD_COL_LEFT_ALT	10
#define KEYBOARD_MASK_LEFT_ALT	0x40
#define KEYBOARD_COL_REFRESH	2
#define KEYBOARD_MASK_REFRESH	0x04
#define KEYBOARD_COL_RIGHT_ALT	10
#define KEYBOARD_MASK_RIGHT_ALT	0x01
#define KEYBOARD_COL_VOL_UP	4
#define KEYBOARD_MASK_VOL_UP	0x01

#endif  /* __CROS_EC_KEYBOARD_CONFIG_H */
