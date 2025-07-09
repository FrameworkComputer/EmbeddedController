/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "fw_config.h"
#include "keyboard_scan.h"
#include "timer.h"

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 50,
	.debounce_down_us = 15 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.stable_scan_period_us = 9 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x1c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

static const struct ec_response_keybd_config taeko_kb = {
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

static const struct ec_response_keybd_config tarlo_kb = {
	.num_top_row_keys = 11,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_MICMUTE,             /* T8 */
		TK_VOL_MUTE,		/* T9 */
		TK_VOL_DOWN,		/* T10 */
		TK_VOL_UP,		/* T11 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY | KEYBD_CAP_NUMERIC_KEYPAD,
};

/*
 * Row Column info for Top row keys T1 - T15.
 * Since tarlo keyboard top row keys have some issue when press with search
 * key together.
 * Needs to add row and col setting for top row.
 * Change T8 row, col to (0,1)
 * Change T9 row, col to (1,5)
 * Change T10 row, col to (3,5)
 * Change T11 row, col to (0,9)
 */
__override struct key {
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
	{ .row = 0, .col = 1 }, /* T8 */
	{ .row = 1, .col = 5 }, /* T9 */
	{ .row = 3, .col = 5 }, /* T10 */
	{ .row = 0, .col = 9 }, /* T11 */
	{ .row = 2, .col = 9 }, /* T12 */
	{ .row = 1, .col = 9 }, /* T13 */
	{ .row = 0, .col = 4 }, /* T14 */
	{ .row = 0, .col = 11 }, /* T15 */
};
BUILD_ASSERT(ARRAY_SIZE(vivaldi_keys) == MAX_TOP_ROW_KEYS);

BUILD_ASSERT(IS_ENABLED(CONFIG_KEYBOARD_VIVALDI));
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	if (ec_cfg_has_keyboard_number_pad())
		return &tarlo_kb;
	else {
		vivaldi_keys[7].row = 2; /* T8 */
		vivaldi_keys[7].col = 9;
		vivaldi_keys[8].row = 1; /* T9 */
		vivaldi_keys[8].col = 9;
		vivaldi_keys[9].row = 0; /* T10 */
		vivaldi_keys[9].col = 4;
		return &taeko_kb;
	}
}

/* TK_REFRESH is always T2 above, vivaldi_keys are overridden. */
BUILD_ASSERT_REFRESH_RC(3, 2);
