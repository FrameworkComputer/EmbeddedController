/* Copyright 2025 The ChromiumOS Authors
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

/*
 * Keyboard function decided by FW config.
 */
test_export_static void kb_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_TYPE, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d, "
			"assuming FW_KB_TYPE_DEFAULT",
			FW_KB_TYPE);
		val = FW_KB_TYPE_DEFAULT;
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
