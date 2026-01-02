/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

#include <zephyr/drivers/gpio.h>

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
