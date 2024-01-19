/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "ec_commands.h"
#include "gpio/gpio.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <drivers/vivaldi_kbd.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

int8_t board_vivaldi_keybd_idx(void)
{
	uint32_t val;

	cros_cbi_get_fw_config(FW_KB_FEATURE, &val);

	if (val == FW_KB_FEATURE_BL_ABSENT_DEFAULT ||
	    val == FW_KB_FEATURE_BL_ABSENT_US2) {
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_1));
	} else {
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_0));
	}
}

/*
 * Keyboard layout decided by FW config.
 */
test_export_static void kb_layout_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * Retrieve the kb layout config.
	 */
	ret = cros_cbi_get_fw_config(FW_KB_FEATURE, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			FW_KB_FEATURE);
		return;
	}
	/*
	 * If keyboard is US2, we need translate right ctrl
	 * to backslash(\|) key.
	 */
	if (val == FW_KB_FEATURE_BL_ABSENT_US2 ||
	    val == FW_KB_FEATURE_BL_PRESENT_US2)
		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
}
DECLARE_HOOK(HOOK_INIT, kb_layout_init, HOOK_PRIO_POST_FIRST);
