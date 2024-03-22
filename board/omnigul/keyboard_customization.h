/* Copyright 2023 The ChromiumOS Authors
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
#ifdef CONFIG_KEYBOARD_CUSTOMIZATION
#undef KEYBOARD_COLS_MAX
#undef KEYBOARD_ROWS

#define KEYBOARD_COLS_MAX 15
#define KEYBOARD_ROWS 8

#define KEYBOARD_COLS_NO_KEYPAD 13

#endif

/*
 * WARNING: Do not directly modify it. You should call keyboard_raw_set_cols,
 * instead. It checks whether you're eligible or not.
 */
extern uint8_t keyboard_cols;

#define KEYBOARD_ROW_TO_MASK(r) (1 << (r))

/* Columns and masks for keys we particularly care about */
#define KEYBOARD_COL_DOWN 11
#define KEYBOARD_ROW_DOWN 6
#define KEYBOARD_MASK_DOWN KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_DOWN)
#define KEYBOARD_COL_ESC 1
#define KEYBOARD_ROW_ESC 1
#define KEYBOARD_MASK_ESC KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_ESC)
#define KEYBOARD_COL_KEY_H 6
#define KEYBOARD_ROW_KEY_H 1
#define KEYBOARD_MASK_KEY_H KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_KEY_H)
#define KEYBOARD_COL_KEY_R 3
#define KEYBOARD_ROW_KEY_R 7
#define KEYBOARD_MASK_KEY_R KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_KEY_R)
#define KEYBOARD_COL_LEFT_ALT 10
#define KEYBOARD_ROW_LEFT_ALT 6
#define KEYBOARD_MASK_LEFT_ALT KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_LEFT_ALT)
#define KEYBOARD_COL_REFRESH 2
#define KEYBOARD_ROW_REFRESH 3
#define KEYBOARD_MASK_REFRESH KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_REFRESH)
#define KEYBOARD_COL_RIGHT_ALT 10
#define KEYBOARD_ROW_RIGHT_ALT 0
#define KEYBOARD_MASK_RIGHT_ALT KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_RIGHT_ALT)
#define KEYBOARD_DEFAULT_COL_VOL_UP 4
#define KEYBOARD_DEFAULT_ROW_VOL_UP 0
#define KEYBOARD_COL_LEFT_SHIFT 7
#define KEYBOARD_ROW_LEFT_SHIFT 5
#define KEYBOARD_MASK_LEFT_SHIFT KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_LEFT_SHIFT)

void set_keyboard_scancode_set2(void);

#endif /* __KEYBOARD_CUSTOMIZATION_H */
