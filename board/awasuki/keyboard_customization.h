/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard configuration */

#ifndef __KEYBOARD_CUSTOMIZATION_H
#define __KEYBOARD_CUSTOMIZATION_H

/*
 * KEYBOARD_COLS_MAX has the build time column size. It's used to allocate
 * exact spaces for arrays. Actual keyboard scanning is done using
 * keyboard_cols, which holds a runtime column size.
 */
#define KEYBOARD_COLS_MAX 18
#define KEYBOARD_ROWS 8

/* Columns for keys we particularly care about */

/* NOTE: the project is using the key configuration from keyboard.dtsi by
 * default, this file is ignored and will be removed. (b/349304225)
 */
#define KEYBOARD_COL_DOWN 11
#define KEYBOARD_ROW_DOWN 6
#define KEYBOARD_COL_ESC 1
#define KEYBOARD_ROW_ESC 1
#define KEYBOARD_COL_KEY_H 6
#define KEYBOARD_ROW_KEY_H 1
#define KEYBOARD_COL_KEY_R 9
#define KEYBOARD_ROW_KEY_R 7
#define KEYBOARD_COL_LEFT_ALT 13
#define KEYBOARD_ROW_LEFT_ALT 6
#define KEYBOARD_COL_REFRESH 2
#define KEYBOARD_ROW_REFRESH 3
#define KEYBOARD_COL_RIGHT_ALT 10
#define KEYBOARD_ROW_RIGHT_ALT 0
#define KEYBOARD_DEFAULT_COL_VOL_UP 5
#define KEYBOARD_DEFAULT_ROW_VOL_UP 3
#define KEYBOARD_COL_LEFT_SHIFT 7
#define KEYBOARD_ROW_LEFT_SHIFT 5

enum ec_cfg_keyboard_layout { KB_LAYOUT_DEFAULT = 0, KB_LAYOUT_1 = 1 };

#endif /* __KEYBOARD_CUSTOMIZATION_H */
