/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fw_config.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_scan.h"
#include "timer.h"

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 50,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.stable_scan_period_us = 9 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

static const struct ec_response_keybd_config dochi_kb = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

/* TK_REFRESH is always T2 above, vivaldi_keys are not overridden. */
BUILD_ASSERT_REFRESH_RC(3, 2);

BUILD_ASSERT(IS_ENABLED(CONFIG_KEYBOARD_VIVALDI));
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &dochi_kb;
}

void kb_init(void)
{
	if (ec_cfg_is_kb_cfk()) {
		/*
		 * Canadian French keyboard (US layout),
		 *   \| (key 45):     0x0061->0x61->0x56
		 *   r-ctrl (key 64): 0xe014->0x14->0x1d
		 * move key45 (row:2,col:7) to key64 (row:4,col:0)
		 */
		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
	}
}
DECLARE_HOOK(HOOK_INIT, kb_init, HOOK_PRIO_PRE_DEFAULT);
