/* Copyright 2022 The ChromiumOS Authors
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
#define BANSHEE_KEYBOARD_COL_DOWN 8
#define BANSHEE_KEYBOARD_ROW_DOWN 1
#define BANSHEE_KEYBOARD_COL_ESC 5
#define BANSHEE_KEYBOARD_ROW_ESC 7
#define BANSHEE_KEYBOARD_COL_KEY_H 7
#define BANSHEE_KEYBOARD_ROW_KEY_H 2
#define BANSHEE_KEYBOARD_COL_KEY_R 6
#define BANSHEE_KEYBOARD_ROW_KEY_R 6
#define BANSHEE_KEYBOARD_COL_LEFT_ALT 3
#define BANSHEE_KEYBOARD_ROW_LEFT_ALT 1
#define BANSHEE_KEYBOARD_COL_REFRESH 4
#define BANSHEE_KEYBOARD_ROW_REFRESH 6
#define BANSHEE_KEYBOARD_COL_ID2_REFRESH 5
#define BANSHEE_KEYBOARD_ROW_ID2_REFRESH 2
#define BANSHEE_KEYBOARD_COL_RIGHT_ALT 3
#define BANSHEE_KEYBOARD_ROW_RIGHT_ALT 0
#define KEYBOARD_DEFAULT_COL_VOL_UP 13
#define KEYBOARD_DEFAULT_ROW_VOL_UP 3
#define BANSHEE_KEYBOARD_COL_LEFT_SHIFT 9
#define BANSHEE_KEYBOARD_ROW_LEFT_SHIFT 1

void board_id_keyboard_col_inverted(int board_id);

#endif /* __KEYBOARD_CUSTOMIZATION_H */
