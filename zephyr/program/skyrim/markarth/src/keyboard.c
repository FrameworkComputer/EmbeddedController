/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"

/*
 * Keyboard layout decided by FW config.
 */
static void kb_layout_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_LAYOUT, &val);
	/*
	 * If keyboard is ANSI(KEYBOARD_ANSI), we need translate make code 64
	 * to 45.And translate 29 to 42
	 */
	if (val == KEYBOARD_ANSI) {
		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
		set_scancode_set2(3, 11, get_scancode_set2(4, 10));
	}
}
DECLARE_HOOK(HOOK_INIT, kb_layout_init, HOOK_PRIO_POST_FIRST);
