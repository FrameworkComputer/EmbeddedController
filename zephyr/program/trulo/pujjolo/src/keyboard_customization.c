/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <drivers/scancode_set2.h>
#include <drivers/vivaldi_kbd.h>

LOG_MODULE_REGISTER(trulo_keyboard, LOG_LEVEL_INF);

static bool key_numpad = FW_KB_NUMERIC_PAD_ABSENT;
/*
 * Keyboard function decided by FW config.
 */
test_export_static void keyboard_matrix_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_NUMERIC_PAD, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d, "
			"assuming FW_KB_NUMERIC_PAD_PRESENT",
			FW_KB_NUMERIC_PAD);
		val = FW_KB_NUMERIC_PAD_PRESENT;
	}

	switch (val) {
	case FW_KB_NUMERIC_PAD_PRESENT:
		key_numpad = FW_KB_NUMERIC_PAD_PRESENT;
		LOG_INF("numpad keyboard matrix");
		break;
	case FW_KB_NUMERIC_PAD_ABSENT:
		set_scancode_set2_table(SCANCODE_SET2_IDX(scancodes_numpad));
		key_numpad = FW_KB_NUMERIC_PAD_ABSENT;
		LOG_INF("no numpad keyboard matrix");
		break;
	default:
		LOG_WRN("invalid cbi value: %x", val);
		return;
	}
}
DECLARE_HOOK(HOOK_INIT, keyboard_matrix_init, HOOK_PRIO_POST_FIRST);

int8_t board_vivaldi_keybd_idx(void)
{
	int kb_status;

	kb_status = key_numpad;

	switch (kb_status) {
	case FW_KB_NUMERIC_PAD_ABSENT:
		return VIVALDI_CFG_IDX(kbd_config_0);
	case FW_KB_NUMERIC_PAD_PRESENT:
		return VIVALDI_CFG_IDX(kbd_config_1);
	default:
		return VIVALDI_CFG_IDX(kbd_config_0);
	}
}

#ifdef CONFIG_KEYBOARD_DEBUG
static uint8_t keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
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
	  KLLI_UNKNO, KLLI_SEARC, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
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
