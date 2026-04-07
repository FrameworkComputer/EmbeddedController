/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(moonstone_keyboard, LOG_LEVEL_INF);

static void kb_layout_init(void)
{
	if (cros_cbi_ufsc_check_match(CBI_UFSC_VALUE_ID(
		    DT_NODELABEL(ufsc_keyboard_layout_ca_fr)))) {
		/*
		 * Canadian French keyboard (US layout),
		 *   \| (key 45):     0x0061->0x61->0x56
		 *   r-ctrl (key 64): 0xe014->0x14->0x1d
		 * move key45 (row:7,col:17) to key64 (row:3,col:14)
		 */
		set_scancode_set2(3, 14, get_scancode_set2(7, 17));
		LOG_INF("CBI USFC: FW_KB_LAYOUT_US2");
	}
}
DECLARE_HOOK(HOOK_INIT, kb_layout_init, HOOK_PRIO_POST_FIRST);

#ifdef CONFIG_KEYBOARD_DEBUG
static uint8_t keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_SEARC, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_F11, KLLI_ESC, KLLI_TAB, '~', 'a', '1', 'z', 'u' },
	{ KLLI_F1, KLLI_F4, KLLI_F3, KLLI_F2, 'd', ',', '3', 'i' },
	{ 'b', 'g', 't', '5', 'f', '.', '4', 'o' },
	{ KLLI_F10, KLLI_F7, KLLI_F6, 's', KLLI_F5, '/', '2', 'p' },
	{ 'n', KLLI_F12, ']', KLLI_F13, 'k', 'c', '8', 'q' },
	{ KLLI_UNKNO, 'h', 'y', '-', 'j', KLLI_SPACE, '0', 'w' },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_L_SHT, KLLI_UNKNO, KLLI_R_SHT },
	{ '=', '\'', '[', '6', ';', 'x', '7', 'e' },
	{ KLLI_UNKNO, KLLI_F9, KLLI_UNKNO, KLLI_UNKNO, 'l', 'v', '9',
	  'r' }, /*delete at 2*/
	{ KLLI_R_ALT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_F14, KLLI_B_SPC, KLLI_F8, KLLI_UNKNO, KLLI_ENTER, 'm', KLLI_DOWN,
	  KLLI_UP },
	{ KLLI_F15, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_RIGHT, KLLI_LEFT },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_L_ALT, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
};

uint8_t get_keycap_label(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS) {
		return keycap_label[col][row];
	}
	return KLLI_UNKNO;
}

void set_keycap_label(uint8_t row, uint8_t col, uint8_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS) {
		keycap_label[col][row] = val;
	}
}
#endif
