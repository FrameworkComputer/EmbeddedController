/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <drivers/scancode_set2.h>
#include <drivers/vivaldi_kbd.h>

LOG_MODULE_REGISTER(brox_keyboard, LOG_LEVEL_INF);

/*
 * Keyboard matrix decided by FW config
 */
test_export_static void keyboard_matrix_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_LAYOUT, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field FW_KB_LAYOUT, "
			"assuming FW_KB_LAYOUT_DEFAULT");
		val = FW_KB_LAYOUT_DEFAULT;
	}

	switch (val) {
	case FW_KB_LAYOUT_US2:
		set_scancode_set2_table(SCANCODE_SET2_IDX(scancodes_us2));
		LOG_INF("KB layout: US2!!!");
		break;
	case FW_KB_LAYOUT_DEFAULT:
	default:
		/* Default keyboard layout */
		LOG_INF("KB layout: default!!!");
		return;
	}
}
DECLARE_HOOK(HOOK_INIT, keyboard_matrix_init, HOOK_PRIO_POST_FIRST);

/*
 * Vivaldi keyboard decided by FW config
 */
int8_t board_vivaldi_keybd_idx(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_BL, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field FW_KB_BL, "
			"assuming FW_KB_BL_ABSENT");
		val = FW_KB_BL_ABSENT;
	}

	switch (val) {
	case FW_KB_BL_PRESENT:
		LOG_INF("KB_BL present!!!");
		return VIVALDI_CFG_IDX(kbd_config_1);
	case FW_KB_BL_ABSENT:
	default:
		LOG_INF("KB_BL absent!!!");
		return VIVALDI_CFG_IDX(kbd_config_0);
	}
}
