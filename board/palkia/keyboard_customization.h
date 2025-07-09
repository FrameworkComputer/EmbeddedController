/* Copyright 2020 The ChromiumOS Authors
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
#define KEYBOARD_COLS_MAX 16
#define KEYBOARD_ROWS 8

/* Columns for keys we particularly care about */
#define KEYBOARD_COL_DOWN 8
#define KEYBOARD_ROW_DOWN 1
#define KEYBOARD_COL_ESC 5
#define KEYBOARD_ROW_ESC 7
#define KEYBOARD_COL_KEY_H 7
#define KEYBOARD_ROW_KEY_H 2
#define KEYBOARD_COL_KEY_R 6
#define KEYBOARD_ROW_KEY_R 6
#define KEYBOARD_COL_LEFT_ALT 3
#define KEYBOARD_ROW_LEFT_ALT 1
#define KEYBOARD_COL_REFRESH 4
#define KEYBOARD_ROW_REFRESH 6
#define KEYBOARD_COL_RIGHT_ALT 3
#define KEYBOARD_ROW_RIGHT_ALT 0
#define KEYBOARD_DEFAULT_COL_VOL_UP 8
#define KEYBOARD_DEFAULT_ROW_VOL_UP 4
#define KEYBOARD_COL_LEFT_SHIFT 9
#define KEYBOARD_ROW_LEFT_SHIFT 1

#endif /* __KEYBOARD_CUSTOMIZATION_H */
