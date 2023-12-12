/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fw_config.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_customization.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "timer.h"

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/* Increase from 50 us, because KSO_02 passes through the H1. */
	.output_settle_us = 80,
	/* Other values should be the same as the default configuration. */
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x1c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xa4, 0xff, 0xf7, 0x55, 0xfb, 0xca  /* full set */
	},
};

static const struct ec_response_keybd_config omnigul_kb = {
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

static const struct ec_response_keybd_config omniknight_kb = {
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
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY | KEYBD_CAP_NUMERIC_KEYPAD,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	if (!ec_cfg_has_keyboard_numpad())
		return &omnigul_kb;
	else
		return &omniknight_kb;
}

/*
 * Row Column info for Top row keys T1 - T15.
 * on mithrax_kb keyboard Row Column is customization
 * need define row col to mapping matrix layout.
 */
__override const struct key {
	uint8_t row;
	uint8_t col;
} vivaldi_keys[] = {
	{ .row = 0, .col = 2 }, /* T1 */
	{ .row = 3, .col = 2 }, /* T2 */
	{ .row = 2, .col = 2 }, /* T3 */
	{ .row = 1, .col = 2 }, /* T4 */
	{ .row = 3, .col = 4 }, /* T5 */
	{ .row = 2, .col = 4 }, /* T6 */
	{ .row = 1, .col = 4 }, /* T7 */
	{ .row = 2, .col = 9 }, /* T8 */
	{ .row = 1, .col = 9 }, /* T9 */
	{ .row = 0, .col = 4 }, /* T10 */
	{ .row = 3, .col = 0 }, /* T11 */
	{ .row = 1, .col = 5 }, /* T12 */
	{ .row = 3, .col = 5 }, /* T13 */
	{ .row = 0, .col = 9 }, /* T14 */
	{ .row = 0, .col = 11 }, /* T15 */
};
BUILD_ASSERT(ARRAY_SIZE(vivaldi_keys) == MAX_TOP_ROW_KEYS);

void kb_init(void)
{
	set_keyboard_scancode_set2();

	if (!ec_cfg_has_keyboard_numpad()) {
		/* Disable scanning KSO13 and 14 if keypad isn't present */
		keyboard_raw_set_cols(KEYBOARD_COLS_NO_KEYPAD);
	} else {
		/* Setting scan mask KSO11, KSO12, KSO13 and KSO14 */
		keyscan_config.actual_key_mask[11] = 0xfe;
		keyscan_config.actual_key_mask[12] = 0xff;
		keyscan_config.actual_key_mask[13] = 0xff;
		keyscan_config.actual_key_mask[14] = 0xff;
	}

	if (ec_cfg_keyboard_layout() == KEYBOARD_ANSI) {
		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
		set_scancode_set2(3, 11, get_scancode_set2(4, 10));
	}
}
DECLARE_HOOK(HOOK_INIT, kb_init, HOOK_PRIO_PRE_DEFAULT);
