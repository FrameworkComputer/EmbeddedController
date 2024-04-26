/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "button.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "ec_commands.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "nissa_sub_board.h"

#include <drivers/vivaldi_kbd.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static bool key_pad = FW_KB_NUMERIC_PAD_ABSENT;

int8_t board_vivaldi_keybd_idx(void)
{
	if (key_pad == FW_KB_NUMERIC_PAD_ABSENT) {
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_0));
	} else {
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_1));
	}
}

/*
 * Keyboard function decided by FW config.
 */
test_export_static void kb_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_NUMERIC_PAD, &val);

	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			FW_KB_NUMERIC_PAD);
	}

	if (val == FW_KB_NUMERIC_PAD_ABSENT) {
		/* Disable scanning KSO13 & 14 if keypad isn't present. */
		keyboard_raw_set_cols(KEYBOARD_COLS_NO_KEYPAD);
		key_pad = FW_KB_NUMERIC_PAD_ABSENT;
	} else {
		key_pad = FW_KB_NUMERIC_PAD_PRESENT;
		/* Setting scan mask KSO11, KSO12, KSO13 and KSO14 */
		keyscan_config.actual_key_mask[11] = 0xfe;
		keyscan_config.actual_key_mask[12] = 0xff;
		keyscan_config.actual_key_mask[13] = 0xff;
		keyscan_config.actual_key_mask[14] = 0xff;
	}

	ret = cros_cbi_get_fw_config(FW_KB_TYPE, &val);

	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_KB_TYPE);
	}

	if (val == FW_KB_TYPE_CA_FR) {
		/*
		 * Canadian French keyboard (US type),
		 *   \|:     0x0061->0x61->0x56
		 *   r-ctrl: 0xe014->0x14->0x1d
		 */
		uint16_t tmp = get_scancode_set2(4, 0);

		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
		set_scancode_set2(2, 7, tmp);
	}
}
DECLARE_HOOK(HOOK_INIT, kb_init, HOOK_PRIO_POST_FIRST);

test_export_static void buttons_init(void)
{
	int ret;
	uint32_t val;
	enum nissa_sub_board_type sb = nissa_get_sb_type();

	ret = cbi_get_board_version(&val);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI BOARD_VER.");
		return;
	}
	/*
	 * The volume up/down button are exchanged on ver3 USB
	 * sub board.
	 *
	 * LTE:
	 *   volup -> gpioa2, voldn -> gpio93
	 * USB:
	 *   volup -> gpio93, voldn -> gpioa2
	 */
	if (val == 3 && sb == NISSA_SB_C_A) {
		LOG_INF("Volume up/down btn exchanged on ver3 USB sku");
		buttons[BUTTON_VOLUME_UP].gpio = GPIO_VOLUME_DOWN_L;
		buttons[BUTTON_VOLUME_DOWN].gpio = GPIO_VOLUME_UP_L;
	}
}
DECLARE_HOOK(HOOK_INIT, buttons_init, HOOK_PRIO_DEFAULT);

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 },	{ 1, 1 }, { 1, 0 },   { 0, 6 },	  { 0, 7 },
	{ -1, -1 }, { -1, -1 }, { 1, 4 }, { 1, 3 },   { -1, -1 }, { 1, 6 },
	{ 1, 7 },   { 3, 1 },	{ 2, 0 }, { 1, 5 },   { 2, 6 },	  { 2, 7 },
	{ 2, 1 },   { 2, 4 },	{ 2, 5 }, { 1, 2 },   { 2, 3 },	  { 2, 2 },
	{ 3, 0 },   { -1, -1 }, { 0, 4 }, { -1, -1 }, { 8, 2 },	  { -1, -1 },
	{ -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
