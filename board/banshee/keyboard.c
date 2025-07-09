/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
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
		0xff, 0xff, 0xff, 0x03, 0xff, 0xff, 0xff,
		0xff, 0xff, 0x03, 0xff, 0xff, 0x03, 0xff,
		0xff, 0xef  /* full set */
	},
};

static const struct ec_response_keybd_config banshee_kb_id1 = {
	.num_top_row_keys = 12,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_FORWARD,		/* T2 */
		TK_REFRESH,		/* T3 */
		TK_FULLSCREEN,		/* T4 */
		TK_OVERVIEW,		/* T5 */
		TK_SNAPSHOT,		/* T6 */
		TK_BRIGHTNESS_DOWN,	/* T7 */
		TK_BRIGHTNESS_UP,	/* T8 */
		TK_PLAY_PAUSE,		/* T9 */
		TK_VOL_MUTE,		/* T10 */
		TK_VOL_DOWN,		/* T11 */
		TK_VOL_UP,		/* T12 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

static const struct ec_response_keybd_config banshee_kb_id2 = {
	.num_top_row_keys = 12,
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
		TK_VOL_MUTE,		/* T10 */
		TK_VOL_DOWN,		/* T11 */
		TK_VOL_UP,		/* T12 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

BUILD_ASSERT(IS_ENABLED(CONFIG_KEYBOARD_VIVALDI));
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	if (get_board_id() <= 1)
		return &banshee_kb_id1;
	else
		return &banshee_kb_id2;
}

/*
 * Row Column info for Top row keys T1 - T15.
 * on banshee keyboard Row Column is customization
 * need define row col to mapping matrix layout.
 */
__override const struct key {
	uint8_t row;
	uint8_t col;
} vivaldi_keys[] = {
	{ .row = 3, .col = 5 }, /* T1 */
	{ .row = 2, .col = 5 }, /* T2 */
	{ .row = 6, .col = 4 }, /* T3 */
	{ .row = 3, .col = 4 }, /* T4 */
	{ .row = 4, .col = 10 }, /* T5 */
	{ .row = 3, .col = 10 }, /* T6 */
	{ .row = 2, .col = 10 }, /* T7 */
	{ .row = 1, .col = 15 }, /* T8 */
	{ .row = 3, .col = 11 }, /* T9 */
	{ .row = 4, .col = 8 }, /* T10 */
	{ .row = 6, .col = 8 }, /* T11 */
	{ .row = 3, .col = 13 }, /* T12 */
	{ .row = 3, .col = 5 }, /* T13 */
	{ .row = 0, .col = 9 }, /* T14 */
	{ .row = 0, .col = 11 }, /* T15 */
};
BUILD_ASSERT(ARRAY_SIZE(vivaldi_keys) == MAX_TOP_ROW_KEYS);

/* TODO(b/219051027): Add assert to check that key_typ.{row,col}_refresh == the
 * row/col in this table. */
