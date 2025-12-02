/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_backlight.h"
#include "keyboard_config.h"
#include "tablet_mode.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(board_init, LOG_LEVEL_INF);

static bool has_backlight = FW_KB_BL_NOT_PRESENT;
int8_t board_vivaldi_keybd_idx(void)
{
	if (has_backlight == FW_KB_BL_NOT_PRESENT) {
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

	ret = cros_cbi_get_fw_config(FW_KB_BL, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_KB_BL);
		return;
	}

	if (val == FW_KB_BL_PRESENT) {
		LOG_INF("CBI FW_CONFIG: FW_KB_BL_PRESENT.");
		has_backlight = FW_KB_BL_PRESENT;
	} else {
		LOG_INF("CBI FW_CONFIG: FW_KB_BL_NOT_PRESENT.");
		has_backlight = FW_KB_BL_NOT_PRESENT;
		kblight_enable(0);
	}
}
DECLARE_HOOK(HOOK_INIT, kb_init, HOOK_PRIO_POST_I2C);

/*
 * Keyboard function decided by FW config.
 */
test_export_static void kb_layout_init(void)
{
	int ret, tmp;
#ifdef CONFIG_KEYBOARD_DEBUG
	int label;
#endif
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_LAYOUT, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_KB_LAYOUT field %d",
			FW_KB_LAYOUT);
		return;
	}
	/*
	 * If keyboard is US2(FW_KB_LAYOUT_US2), we need translate right ctrl
	 * to Europe2 key.
	 */
	if (val == FW_KB_LAYOUT_US2) {
		set_scancode_set2(3, 14, get_scancode_set2(2, 7));
#ifdef CONFIG_KEYBOARD_DEBUG
		set_keycap_label(3, 14, get_keycap_label(2, 7));
#endif
		LOG_INF("CBI FW_CONFIG: FW_KB_LAYOUT_US2");
	}

	/*
	 * If keyboard is JP(FW_KB_LAYOUT_JP), we need translate right alt,
	 * right fn and henkan key.
	 */
	if (val == FW_KB_LAYOUT_JP) {
		tmp = get_scancode_set2(0, 10);
#ifdef CONFIG_KEYBOARD_DEBUG
		label = get_keycap_label(0, 10);
#endif
		set_scancode_set2(0, 10, get_scancode_set2(5, 15));
#ifdef CONFIG_KEYBOARD_DEBUG
		set_keycap_label(0, 10, get_keycap_label(5, 15));
#endif
		set_scancode_set2(0, 13, get_scancode_set2(5, 15));
#ifdef CONFIG_KEYBOARD_DEBUG
		set_keycap_label(0, 13, get_keycap_label(5, 15));
#endif
		set_scancode_set2(5, 15, get_scancode_set2(1, 12));
#ifdef CONFIG_KEYBOARD_DEBUG
		set_keycap_label(5, 15, get_keycap_label(1, 12));
#endif
		set_scancode_set2(1, 12, tmp);
#ifdef CONFIG_KEYBOARD_DEBUG
		set_keycap_label(1, 12, label);
#endif
		LOG_INF("CBI FW_CONFIG: FW_KB_LAYOUT_JP");
	}
}
DECLARE_HOOK(HOOK_INIT, kb_layout_init, HOOK_PRIO_POST_I2C);

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	/*
	 * Remove keyboard backlight feature for devices that don't support it.
	 */

	if (has_backlight == FW_KB_BL_NOT_PRESENT)
		return (flags0 & ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB));
	else
		return flags0;
}
