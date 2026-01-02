/* Copyright 2024 The ChromiumOS Authors
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

#include <drivers/scancode_set2.h>

LOG_MODULE_REGISTER(rull_keyboard, LOG_LEVEL_INF);

/*
 * Keyboard function decided by FW config.
 */
test_export_static void keyboard_matrix_init(void)
{
	int ret;
	uint32_t val;
	bool kb_numeric_pad;

	ret = cros_cbi_get_fw_config(FW_KB_NUMPAD, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d, "
			"assuming FW_KB_NUMERIC_PAD_PRESENT",
			FW_KB_NUMPAD);
		return;
	}

	switch (val) {
	case FW_KB_NUMPAD_PRESENT:
		kb_numeric_pad = FW_KB_NUMPAD_PRESENT;
		set_scancode_set2_table(SCANCODE_SET2_IDX(scancodes_numpad));
		LOG_INF("numpad keyboard matrix");
		break;
	case FW_KB_NUMPAD_NOT_PRESENT:
		kb_numeric_pad = FW_KB_NUMPAD_NOT_PRESENT;
		set_scancode_set2_table(SCANCODE_SET2_IDX(scancodes_default));
		LOG_INF("without_numpad keyboard matrix");
		break;
	default:
		LOG_WRN("invalid cbi value: %x", val);
		return;
	}

	ret = cros_cbi_get_fw_config(FW_KB_LAYOUT, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_KB_LAYOUT field %d",
			FW_KB_LAYOUT);
		return;
	}
	/*
	 * If keyboard is US CA-FR(FW_KB_LAYOUT_1), we need translate right ctrl
	 * to backslash(\|) key.
	 */
	if (val == FW_KB_LAYOUT_1) {
		if (kb_numeric_pad == FW_KB_NUMPAD_PRESENT)
			set_scancode_set2(3, 14, get_scancode_set2(2, 7));
		else
			set_scancode_set2(3, 14, get_scancode_set2(7, 17));
	}
}
DECLARE_HOOK(HOOK_INIT, keyboard_matrix_init, HOOK_PRIO_POST_I2C);

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
