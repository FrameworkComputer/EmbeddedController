/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard configuration */

#ifndef __KEYBOARD_CUSTOMIZATION_H
#define __KEYBOARD_CUSTOMIZATION_H

/* Keyboard matrix is 13 (or 15 with keypad) output columns x 8 input rows */
#define KEYBOARD_COLS_WITH_KEYPAD 15
#define KEYBOARD_COLS_NO_KEYPAD 13

/*
 * KEYBOARD_COLS_MAX has the build time column size. It's used to allocate
 * exact spaces for arrays. Actual keyboard scanning is done using
 * keyboard_cols, which holds a runtime column size.
 */
#ifdef CONFIG_KEYBOARD_KEYPAD
#define KEYBOARD_COLS_MAX KEYBOARD_COLS_WITH_KEYPAD
#else
#define KEYBOARD_COLS_MAX KEYBOARD_COLS_NO_KEYPAD
#endif
#define KEYBOARD_ROWS 8

/* Columns for keys we particularly care about */
#define KEYBOARD_COL_DOWN 11
#define KEYBOARD_ROW_DOWN 6
#define KEYBOARD_COL_ESC 1
#define KEYBOARD_ROW_ESC 1
#define KEYBOARD_COL_KEY_H 6
#define KEYBOARD_ROW_KEY_H 1
#define KEYBOARD_COL_KEY_R 3
#define KEYBOARD_ROW_KEY_R 7
#define KEYBOARD_COL_LEFT_ALT 13
#define KEYBOARD_ROW_LEFT_ALT 6
#define KEYBOARD_COL_REFRESH 2
#ifdef CONFIG_KEYBOARD_REFRESH_ROW3
#define KEYBOARD_ROW_REFRESH 3
#else
#define KEYBOARD_ROW_REFRESH 2
#endif
#define KEYBOARD_COL_RIGHT_ALT 10
#define KEYBOARD_ROW_RIGHT_ALT 0
#define KEYBOARD_DEFAULT_COL_VOL_UP 4
#define KEYBOARD_DEFAULT_ROW_VOL_UP 0
#define KEYBOARD_COL_LEFT_SHIFT 7
#define KEYBOARD_ROW_LEFT_SHIFT 5

#endif /* __KEYBOARD_CUSTOMIZATION_H */
