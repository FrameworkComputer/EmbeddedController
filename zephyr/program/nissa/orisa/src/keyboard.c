/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "ec_commands.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_customization.h"
#include "keyboard_scan.h"
#include "timer.h"

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Keyboard layout decided by FW config.
 */
test_export_static void kb_layout_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_TYPE, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_KB_TYPE);
		return;
	}
	/*
	 * If keyboard is ANSI(KEYBOARD_ANSI), we need translate make code 64
	 * to 45.And translate 29 to 42
	 */
	if (val == FW_KB_TYPE_ANSI_CANADIAN) {
		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
		set_scancode_set2(3, 11, get_scancode_set2(4, 10));
	}
}
DECLARE_HOOK(HOOK_INIT, kb_layout_init, HOOK_PRIO_POST_FIRST);
