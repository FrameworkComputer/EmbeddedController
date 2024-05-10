/* Copyright 2024 The ChromiumOS Authors
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
	{ 0x0058, 0x0076, 0x000d, 0x000e, 0x001c, 0x001a, 0x0016, 0x0015 },
	{ 0x0005, 0x000c, 0x0004, 0x0006, 0x0023, 0x0021, 0x0026, 0x0024 },
	{ 0x0000, 0xe01f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	{ 0x0032, 0x0034, 0x002c, 0x002e, 0x002b, 0x002a, 0x0025, 0x002d },
	{ 0x000a, 0x0083, 0x000b, 0x0003, 0x001b, 0x0022, 0x001e, 0x001d },
	{ 0x0000, 0x0000, 0x005b, 0x0000, 0x0042, 0x0041, 0x003e, 0x0043 },
	{ 0x0000, 0x0000, 0x0000, 0xe01f, 0x0000, 0x0000, 0x0000, 0x0000 },
	{ 0x0031, 0x0033, 0x0035, 0x0036, 0x003b, 0x003a, 0x003d, 0x003c },
	{ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0012, 0x0000, 0x0059 },
	{ 0x0055, 0x0052, 0x0054, 0x004e, 0x004c, 0x004a, 0x0045, 0x004d },
	{ 0x0007, 0x0078, 0x0009, 0x0001, 0x004b, 0x0049, 0x0046, 0x0044 },
	{ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	{ 0xe011, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0011, 0x0000 },
	{ 0x0000, 0x0066, 0x0000, 0x005d, 0x005a, 0x0029, 0xe072, 0xe075 },
	{ 0xe071, 0xe07c, 0xe06c, 0xe07d, 0xe07a, 0xe069, 0xe074, 0xe06b },
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
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_L_CTR, KLLI_UNKNO, KLLI_R_CTR,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_ESC, KLLI_TAB, '`', 'a', 'z', '1', 'q' },
	{ KLLI_F1, KLLI_F4, KLLI_F3, KLLI_F2, 'd', 'c', '3', 'e' },
	{ KLLI_UNKNO, KLLI_SEARC, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ 'b', 'g', 't', '5', 'f', 'v', '4', 'r' },
	{ KLLI_F8, KLLI_F7, KLLI_F6, KLLI_F5, 's', 'x', '2', 'w' },
	{ KLLI_UNKNO, KLLI_UNKNO, ']', KLLI_UNKNO, 'k', ',', '8', 'i' },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ 'n', 'h', 'y', '6', 'j', 'm', '7', 'u' },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_L_SHT, KLLI_UNKNO, KLLI_R_SHT },
	{ '=', '"', '[', '-', ';', '/', '0', 'p' },
	{ KLLI_F12, KLLI_F11, KLLI_F10, KLLI_F9, 'l', '.', '9', 'o' },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_R_ALT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_L_ALT, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_B_SPC, KLLI_UNKNO, '\\', KLLI_ENTER, KLLI_SPACE,
	  KLLI_DOWN, KLLI_UP },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_RIGHT, KLLI_LEFT },
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
