/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "keyboard_customization.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

static uint16_t scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ 0x0000, 0x0000, 0x0014, 0xe01f, 0xe014, 0x0000, 0x0000, 0x0000 },
	{ 0x001f, 0x0076, 0x0017, 0x000e, 0x001c, 0x003a, 0x000d, 0x0016 },
	{ 0x006c, 0xe024, 0xe01d, 0xe020, 0xe038, 0xe071, 0x0026, 0x002a },
	{ 0x0032, 0x0034, 0x002c, 0x002e, 0x002b, 0x0029, 0x0025, 0x002d },
	{ 0x0078, 0xe032, 0xe035, 0xe02c, 0xe02d, 0x0041, 0x001e, 0x001d },
	{ 0x0051, 0x0007, 0x005b, 0x000f, 0x0042, 0x0022, 0x003e, 0x0043 },
	{ 0x0031, 0x0033, 0x0035, 0x0036, 0x003b, 0x001b, 0x003d, 0x003c },
	{ 0x0000, 0x0012, 0x0061, 0x0000, 0x0000, 0x0000, 0x0000, 0x0059 },
	{ 0x0055, 0x0052, 0x0054, 0x004e, 0x004c, 0x0024, 0x0044, 0x004d },
	{ 0x0045, 0xe021, 0xe023, 0x002f, 0x004b, 0x0049, 0x0046, 0x001a },
	{ 0xe011, 0x0000, 0x006a, 0x0000, 0x005d, 0x0000, 0x0011, 0x0000 },
	{ 0xe07a, 0x005d, 0xe075, 0x006b, 0x005a, 0xe072, 0x004a, 0x0066 },
	{ 0xe06b, 0xe074, 0xe069, 0x0067, 0xe06c, 0x0064, 0x0015, 0xe07d },
	{ 0x0073, 0x007c, 0x007b, 0x0074, 0x0071, 0xe04a, 0x0070, 0x0021 },
	{ 0x0023, 0xe05a, 0x0075, 0x0079, 0x007a, 0x0072, 0x007d, 0x0069 },
};

uint16_t get_scancode_set2(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		return scancode_set2[col][row];
	return 0;
}

void set_scancode_set2(uint8_t row, uint8_t col, uint16_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		scancode_set2[col][row] = val;
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

#ifdef CONFIG_KEYBOARD_DEBUG
static char keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ 'c', KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ 'q', KLLI_UNKNO, KLLI_UNKNO, KLLI_TAB, '`', '1', KLLI_UNKNO, 'a' },
	{ KLLI_R_ALT, KLLI_L_ALT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_SPACE, 'e', KLLI_F4, KLLI_SEARC, '3', KLLI_F3,
	  KLLI_UNKNO },
	{ 'x', 'z', KLLI_F2, KLLI_F1, 's', '2', 'w', KLLI_ESC },
	{ 'v', 'b', 'g', 't', '5', '4', 'r', 'f' },
	{ 'm', 'n', 'h', 'y', '6', '7', 'u', 'j' },
	{ '.', KLLI_DOWN, '\\', 'o', KLLI_F10, '9', KLLI_UNKNO, 'l' },
	{ KLLI_R_SHT, KLLI_L_SHT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ ',', KLLI_UNKNO, KLLI_F7, KLLI_F6, KLLI_F5, '8', 'i', 'k' },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_F9, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_LEFT, KLLI_UNKNO },
	{ KLLI_R_CTR, KLLI_L_CTR, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ '/', KLLI_UP, '-', KLLI_UNKNO, '0', 'p', '[', ';' },
	{ '\'', KLLI_ENTER, KLLI_UNKNO, KLLI_UNKNO, '=', KLLI_B_SPC, ']', 'd' },
	{ KLLI_UNKNO, KLLI_F8, KLLI_RIGHT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO },
};

char get_keycap_label(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		return keycap_label[col][row];
	return KLLI_UNKNO;
}

void set_keycap_label(uint8_t row, uint8_t col, char val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		keycap_label[col][row] = val;
}
#endif
