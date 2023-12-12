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

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

test_export_static const struct ec_response_keybd_config
	joxer_kb_w_kb_light = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_KBD_BKLIGHT_TOGGLE,	/* T8 */
		TK_PLAY_PAUSE,		/* T9 */
		TK_MICMUTE,		/* T10 */
		TK_VOL_MUTE,		/* T11 */
		TK_VOL_DOWN,		/* T12 */
		TK_VOL_UP,		/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

test_export_static const struct ec_response_keybd_config
	joxer_kb_wo_kb_light = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_PLAY_PAUSE,		/* T8 */
		TK_MICMUTE,		/* T9 */
		TK_VOL_MUTE,		/* T10 */
		TK_VOL_DOWN,		/* T11 */
		TK_VOL_UP,		/* T12 */
		TK_MENU,		/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	uint32_t val;

	cros_cbi_get_fw_config(FW_KB_FEATURE, &val);

	if (val == FW_KB_FEATURE_BL_ABSENT_DEFAULT ||
	    val == FW_KB_FEATURE_BL_ABSENT_US2)
		return &joxer_kb_wo_kb_light;
	else
		return &joxer_kb_w_kb_light;
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
