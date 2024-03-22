/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_ssfc.h"
#include "common.h"
#include "gpio.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_customization.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"

static uint16_t (*scancode_set2)[KEYBOARD_ROWS];

static uint16_t KB2scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ 0x0000, 0x0000, 0x0014, 0xe01f, 0xe014, 0x0000, 0x0000, 0x0000 },
	{ 0x0000, 0x0076, 0x0000, 0x000e, 0x001c, 0x003a, 0x000d, 0x0016 },
	{ 0x006c, 0x000c, 0x0004, 0x0006, 0x0005, 0xe071, 0x0026, 0x002a },
	{ 0x0032, 0x0034, 0x002c, 0x002e, 0x002b, 0x0029, 0x0025, 0x002d },
	{ 0xe01f, 0x0009, 0x0083, 0x000b, 0x0003, 0x0041, 0x001e, 0x001d },
	{ 0x0051, 0x0000, 0x005b, 0x0000, 0x0042, 0x0022, 0x003e, 0x0043 },
	{ 0x0031, 0x0033, 0x0035, 0x0036, 0x003b, 0x001b, 0x003d, 0x003c },
	{ 0x0000, 0x0012, 0x0061, 0x0000, 0x0000, 0x0000, 0x0000, 0x0059 },
	{ 0x0055, 0x0052, 0x0054, 0x004e, 0x004c, 0x0024, 0x0044, 0x004d },
	{ 0x0045, 0x0001, 0x000a, 0x002f, 0x004b, 0x0049, 0x0046, 0x001a },
	{ 0xe011, 0x0000, 0x006a, 0x0000, 0x005d, 0x0000, 0x0011, 0x0000 },
	{ 0xe07a, 0x005d, 0xe075, 0x006b, 0x005a, 0xe072, 0x004a, 0x0066 },
	{ 0xe06b, 0xe074, 0xe069, 0x0067, 0xe06c, 0x0064, 0x0015, 0xe07d },
	{ 0x0073, 0x007c, 0x007b, 0x0074, 0x0071, 0xe04a, 0x0070, 0x0021 },
	{ 0x0023, 0xe05a, 0x0075, 0x0079, 0x007a, 0x0072, 0x007D, 0x0069 },
};

/* The standard Chrome OS keyboard matrix table in scan code set 2. */
static uint16_t KB1scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ 0x0000, 0x0000, 0x0014, 0xe01f, 0xe014, 0xe007, 0x0000, 0x0000 },
	{ 0xe01f, 0x0076, 0x000d, 0x000e, 0x001c, 0x001a, 0x0016, 0x0015 },
	{ 0x0005, 0x000c, 0x0004, 0x0006, 0x0023, 0x0021, 0x0026, 0x0024 },
	{ 0x0032, 0x0034, 0x002c, 0x002e, 0x002b, 0x002a, 0x0025, 0x002d },
	{ 0x0009, 0x0083, 0x000b, 0x0003, 0x001b, 0x0022, 0x001e, 0x001d },
	{ 0x0051, 0x0000, 0x005b, 0x0000, 0x0042, 0x0041, 0x003e, 0x0043 },
	{ 0x0031, 0x0033, 0x0035, 0x0036, 0x003b, 0x003a, 0x003d, 0x003c },
	{ 0x0000, 0x0000, 0x0061, 0x0000, 0x0000, 0x0012, 0x0000, 0x0059 },
	{ 0x0055, 0x0052, 0x0054, 0x004e, 0x004c, 0x004a, 0x0045, 0x004d },
	{ 0x0000, 0x0001, 0x000a, 0x002f, 0x004b, 0x0049, 0x0046, 0x0044 },
	{ 0xe011, 0x0000, 0x006a, 0x0000, 0x005d, 0x0000, 0x0011, 0x0000 },
#ifndef CONFIG_KEYBOARD_KEYPAD
	{ 0x0000, 0x0066, 0x0000, 0x005d, 0x005a, 0x0029, 0xe072, 0xe075 },
	{ 0x0000, 0x0064, 0x0000, 0x0067, 0x0000, 0x0000, 0xe074, 0xe06b },
#else
	{ 0x0000, 0x0066, 0xe071, 0x005d, 0x005a, 0x0029, 0xe072, 0xe075 },
	{ 0xe06c, 0x0064, 0xe07d, 0x0067, 0xe069, 0xe07a, 0xe074, 0xe06b },
	{ 0xe04a, 0x007c, 0x007b, 0x0074, 0x0071, 0x0073, 0x006b, 0x0070 },
	{ 0x006c, 0x0075, 0x007d, 0x0079, 0x007a, 0x0072, 0x0069, 0xe05a },
#endif
};

uint16_t get_scancode_set2(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS) {
		return *(*(scancode_set2 + col) + row);
	}
	return 0;
}

void set_scancode_set2(uint8_t row, uint8_t col, uint16_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS) {
		*(*(scancode_set2 + col) + row) = val;
	}
}

void board_keyboard_drive_col(int col)
{
	/* Drive all lines to high */
	if (col == KEYBOARD_COLUMN_NONE)
		gpio_set_level(GPIO_KBD_KSO2, 0);

	/* Set KBSOUT to zero to detect key-press */
	else if (col == KEYBOARD_COLUMN_ALL)
		gpio_set_level(GPIO_KBD_KSO2, 1);

	/* Drive one line for detection */
	else {
		if (col == 2)
			gpio_set_level(GPIO_KBD_KSO2, 1);
		else
			gpio_set_level(GPIO_KBD_KSO2, 0);
	}
}

struct keyboard_type key_typ = {
	.col_esc = KEYBOARD_COL_ESC,
	.row_esc = KEYBOARD_ROW_ESC,
	.col_down = KEYBOARD_COL_DOWN,
	.row_down = KEYBOARD_ROW_DOWN,
	.col_left_shift = KEYBOARD_COL_LEFT_SHIFT,
	.row_left_shift = KEYBOARD_ROW_LEFT_SHIFT,
	.col_refresh = KEYBOARD_COL_REFRESH,
	.row_refresh = KEYBOARD_ROW_REFRESH,
	.col_right_alt = KEYBOARD_COL_RIGHT_ALT,
	.row_right_alt = KEYBOARD_ROW_RIGHT_ALT,
	.col_left_alt = KEYBOARD_COL_LEFT_ALT,
	.row_left_alt = KEYBOARD_ROW_LEFT_ALT,
	.col_key_r = KEYBOARD_COL_KEY_R,
	.row_key_r = KEYBOARD_ROW_KEY_R,
	.col_key_h = KEYBOARD_COL_KEY_H,
	.row_key_h = KEYBOARD_ROW_KEY_H,
};

int keyboard_choose(void)
{
	if (get_cbi_ssfc_keyboard() == SSFC_KEYBOARD_GAMING)
		return 1;

	return 0;
}

void key_choose(void)
{
	if (keyboard_choose() == 1) {
		key_typ.col_esc = KEYBOARD2_COL_ESC;
		key_typ.row_esc = KEYBOARD2_ROW_ESC;
		key_typ.col_down = KEYBOARD2_COL_DOWN;
		key_typ.row_down = KEYBOARD2_ROW_DOWN;
		key_typ.col_left_shift = KEYBOARD2_COL_LEFT_SHIFT;
		key_typ.row_left_shift = KEYBOARD2_ROW_LEFT_SHIFT;
		key_typ.col_refresh = KEYBOARD2_COL_REFRESH;
		key_typ.row_refresh = KEYBOARD2_ROW_REFRESH;
		key_typ.col_right_alt = KEYBOARD2_COL_RIGHT_ALT;
		key_typ.row_right_alt = KEYBOARD2_ROW_RIGHT_ALT;
		key_typ.col_left_alt = KEYBOARD2_COL_LEFT_ALT;
		key_typ.row_left_alt = KEYBOARD2_ROW_LEFT_ALT;
		key_typ.col_key_r = KEYBOARD2_COL_KEY_R;
		key_typ.row_key_r = KEYBOARD2_ROW_KEY_R;
		key_typ.col_key_h = KEYBOARD2_COL_KEY_H;
		key_typ.row_key_h = KEYBOARD2_ROW_KEY_H;

		boot_key_list[BOOT_KEY_ESC].col = KEYBOARD2_COL_ESC;
		boot_key_list[BOOT_KEY_ESC].row = KEYBOARD2_ROW_ESC;
		boot_key_list[BOOT_KEY_DOWN_ARROW].col = KEYBOARD2_COL_DOWN;
		boot_key_list[BOOT_KEY_DOWN_ARROW].row = KEYBOARD2_ROW_DOWN;
		boot_key_list[BOOT_KEY_LEFT_SHIFT].col =
			KEYBOARD2_COL_LEFT_SHIFT;
		boot_key_list[BOOT_KEY_LEFT_SHIFT].row =
			KEYBOARD2_ROW_LEFT_SHIFT;
		boot_key_list[BOOT_KEY_REFRESH].col = KEYBOARD2_COL_REFRESH;
		boot_key_list[BOOT_KEY_REFRESH].row = KEYBOARD2_ROW_REFRESH;

		scancode_set2 = KB2scancode_set2;
	} else {
		key_typ.col_esc = KEYBOARD_COL_ESC;
		key_typ.row_esc = KEYBOARD_ROW_ESC;
		key_typ.col_down = KEYBOARD_COL_DOWN;
		key_typ.row_down = KEYBOARD_ROW_DOWN;
		key_typ.col_left_shift = KEYBOARD_COL_LEFT_SHIFT;
		key_typ.row_left_shift = KEYBOARD_ROW_LEFT_SHIFT;
		key_typ.col_refresh = KEYBOARD_COL_REFRESH;
		key_typ.row_refresh = KEYBOARD_ROW_REFRESH;
		key_typ.col_right_alt = KEYBOARD_COL_RIGHT_ALT;
		key_typ.row_right_alt = KEYBOARD_ROW_RIGHT_ALT;
		key_typ.col_left_alt = KEYBOARD_COL_LEFT_ALT;
		key_typ.row_left_alt = KEYBOARD_ROW_LEFT_ALT;
		key_typ.col_key_r = KEYBOARD_COL_KEY_R;
		key_typ.row_key_r = KEYBOARD_ROW_KEY_R;
		key_typ.col_key_h = KEYBOARD_COL_KEY_H;
		key_typ.row_key_h = KEYBOARD_ROW_KEY_H;

		scancode_set2 = KB1scancode_set2;
	}
}
