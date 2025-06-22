/* Copyright 2020 The Chromium OS Authors. All rights reserved.
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

/*
 * WARNING: Do not directly modify it. You should call keyboard_raw_set_cols,
 * instead. It checks whether you're eligible or not.
 */
extern uint8_t keyboard_cols;

#define KEYBOARD_ROW_TO_MASK(r) (1 << (r))

/* TODO FRAMEWORK - VERIFY THESE ARE CORRECT - they dont match our current layout. */ 

/* Columns and masks for keys we particularly care about */
#define KEYBOARD_COL_DOWN	11
#define KEYBOARD_ROW_DOWN	6
#define KEYBOARD_MASK_DOWN	KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_DOWN)
#define KEYBOARD_COL_ESC	1
#define KEYBOARD_ROW_ESC	1
#define KEYBOARD_MASK_ESC	KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_ESC)
#define KEYBOARD_COL_KEY_H	6
#define KEYBOARD_ROW_KEY_H	1
#define KEYBOARD_MASK_KEY_H	KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_KEY_H)
#define KEYBOARD_COL_KEY_R	3
#define KEYBOARD_ROW_KEY_R	7
#define KEYBOARD_MASK_KEY_R	KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_KEY_R)
#define KEYBOARD_COL_LEFT_ALT	10
#define KEYBOARD_ROW_LEFT_ALT	6
#define KEYBOARD_MASK_LEFT_ALT	KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_LEFT_ALT)
#define KEYBOARD_COL_REFRESH	2
#define KEYBOARD_ROW_REFRESH	2
#define KEYBOARD_MASK_REFRESH	KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_REFRESH)
#define KEYBOARD_COL_RIGHT_ALT	10
#define KEYBOARD_ROW_RIGHT_ALT	0
#define KEYBOARD_MASK_RIGHT_ALT	KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_RIGHT_ALT)
#define KEYBOARD_DEFAULT_COL_VOL_UP	4
#define KEYBOARD_DEFAULT_ROW_VOL_UP	0
#define KEYBOARD_COL_LEFT_CTRL  0
#define KEYBOARD_ROW_LEFT_CTRL  2
#define KEYBOARD_MASK_LEFT_CTRL KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_LEFT_CTRL)
#define KEYBOARD_COL_RIGHT_CTRL 0
#define KEYBOARD_ROW_RIGHT_CTRL 4
#define KEYBOARD_MASK_RIGHT_CTRL KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_RIGHT_CTRL)
#define KEYBOARD_COL_SEARCH     1
#define KEYBOARD_ROW_SEARCH     0
#define KEYBOARD_MASK_SEARCH    KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_SEARCH)
#define KEYBOARD_COL_KEY_0      8
#define KEYBOARD_ROW_KEY_0      6
#define KEYBOARD_MASK_KEY_0     KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_KEY_0)
#define KEYBOARD_COL_KEY_1      1
#define KEYBOARD_ROW_KEY_1      6
#define KEYBOARD_MASK_KEY_1     KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_KEY_1)
#define KEYBOARD_COL_KEY_2      4
#define KEYBOARD_ROW_KEY_2      6
#define KEYBOARD_MASK_KEY_2     KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_KEY_2)
#define KEYBOARD_COL_LEFT_SHIFT 7
#define KEYBOARD_ROW_LEFT_SHIFT 5
#define KEYBOARD_MASK_LEFT_SHIFT KEYBOARD_ROW_TO_MASK(KEYBOARD_ROW_LEFT_SHIFT)

enum kb_fn_table {
	KB_FN_F1 = BIT(0),
	KB_FN_F2 = BIT(1),
	KB_FN_F3 = BIT(2),
	KB_FN_F4 = BIT(3),
	KB_FN_F5 =  BIT(4),
	KB_FN_F6 = BIT(5),
	KB_FN_F7 = BIT(6),
	KB_FN_F8 = BIT(7),
	KB_FN_F9 = BIT(8),
	KB_FN_F10 = BIT(9),
	KB_FN_F11 = BIT(10),
	KB_FN_F12 = BIT(11),
	KB_FN_DELETE = BIT(12),
	KB_FN_K = BIT(13),
	KB_FN_S = BIT(14),
	KB_FN_LEFT = BIT(15),
	KB_FN_RIGHT = BIT(16),
	KB_FN_UP = BIT(17),
	KB_FN_DOWN = BIT(18),
	KB_FN_ESC = BIT(19),
	KB_FN_B = BIT(20),
	KB_FN_P = BIT(21),
	KB_FN_SPACE = BIT(22),
};

#ifdef CONFIG_KEYBOARD_BACKLIGHT
int hx20_kblight_enable(int enable);
#endif

#ifdef CONFIG_FACTORY_SUPPORT
void factory_setting(uint8_t enable);
void factory_power_button(int level);
int factory_status(void);
#endif

#ifdef CONFIG_CAPSLED_SUPPORT
void hx20_8042_led_control(int data);
#endif

void set_display_toggle_key_hid(uint8_t enable);

#endif /* __KEYBOARD_CUSTOMIZATION_H */
