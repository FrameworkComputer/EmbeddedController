/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_customization.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

#include <zephyr/drivers/gpio.h>

static uint16_t scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ 0x0000, 0x0000, 0x0014, 0x0000, 0xe014, 0x0000, 0x0000, 0x0000 },
	{ 0xe01f, 0x0076, 0x000d, 0x000e, 0x001c, 0x001a, 0x0016, 0x0015 },
	{ 0x0005, 0x000c, 0x0004, 0x0006, 0x0023, 0x0021, 0x0026, 0x0024 },
	{ 0x0032, 0x0034, 0x002c, 0x002e, 0x002b, 0x002a, 0x0025, 0x002d },
	{ 0x000a, 0x0083, 0x000b, 0x0003, 0x001b, 0x0022, 0x001e, 0x001d },
	{ 0x0051, 0x0000, 0x005b, 0x0000, 0x0042, 0x0041, 0x003e, 0x0043 },
	{ 0x0031, 0x0033, 0x0035, 0x0036, 0x003b, 0x003a, 0x003d, 0x003c },
	{ 0x0000, 0x0000, 0x0061, 0x0000, 0x0000, 0x0012, 0x0000, 0x0059 },
	{ 0x0055, 0x0052, 0x0054, 0x004e, 0x004c, 0x004a, 0x0045, 0x004d },
	{ 0x0000, 0x0078, 0x0009, 0x002f, 0x004b, 0x0049, 0x0046, 0x0044 },
	{ 0xe011, 0x0000, 0x006a, 0x0000, 0x005d, 0x0000, 0x0011, 0x0000 },
	{ 0x0000, 0x0066, 0x0000, 0x005d, 0x005a, 0x0029, 0xe072, 0xe075 },
	{ 0x0000, 0xe064, 0x0000, 0x0067, 0x0000, 0x0000, 0xe074, 0xe06b },
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

#ifdef CONFIG_KEYBOARD_DEBUG
static uint8_t keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
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

uint8_t get_keycap_label(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		return keycap_label[col][row];
	return KLLI_UNKNO;
}

void set_keycap_label(uint8_t row, uint8_t col, uint8_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		keycap_label[col][row] = val;
}
#endif
