/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "aw20198.h"
#include "common.h"
#include "ec_commands.h"
#include "keyboard_scan.h"
#include "rgb_keyboard.h"
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
		0x1c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xa4, 0xff, 0xff, 0x55, 0xff, 0xff, 0xff, 0xff,  /* full set */
	},
	.ksi_threshold_mv = 250,
};

static const struct ec_response_keybd_config taniks_kb = {
	.num_top_row_keys = 11,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_MICMUTE,		/* T8 */
		TK_VOL_MUTE,		/* T9 */
		TK_VOL_DOWN,		/* T10 */
		TK_VOL_UP,		/* T11 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY | KEYBD_CAP_NUMERIC_KEYPAD,
};

/*
 * Row Column info for Top row keys T1 - T15.
 * For taniks keyboard layout(T11 - T14) and
 * printing(F8 - F11) are different issue.
 * Move T11 - T14 row and col setting to T8 - T11.
 * Need define row col to mapping matrix layout.
 * Change T8 row, col to (0,1)
 * Change T9 row, col to (1,5)
 * Change T10 row, col to (3,5)
 * Change T11 row, col to (0,9)
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

/* TK_REFRESH is always T2 above, vivaldi_keys are overridden. */
BUILD_ASSERT_REFRESH_RC(3, 2);

static struct rgb_s grid0[RGB_GRID0_COL * RGB_GRID0_ROW];

struct rgbkbd rgbkbds[] = {
	[0] = {
		.cfg = &(const struct rgbkbd_cfg) {
			.drv = &aw20198_drv,
			.i2c = I2C_PORT_KBMCU,
			.col_len = RGB_GRID0_COL,
			.row_len = RGB_GRID0_ROW,
		},
		.buf = grid0,
	},
};
const uint8_t rgbkbd_count = ARRAY_SIZE(rgbkbds);

const uint8_t rgbkbd_hsize = RGB_GRID0_COL;
const uint8_t rgbkbd_vsize = RGB_GRID0_ROW;

enum ec_rgbkbd_type rgbkbd_type = EC_RGBKBD_TYPE_FOUR_ZONES_40_LEDS;

#define LED(x, y) RGBKBD_COORD((x), (y))
#define DELM RGBKBD_DELM

const uint8_t rgbkbd_map[] = {
	DELM, LED(0, 0), DELM, LED(1, 0), DELM, LED(2, 0), DELM, LED(3, 0),
	DELM, LED(4, 0), DELM, LED(5, 0), DELM, LED(6, 0), DELM, LED(7, 0),
	DELM, LED(0, 1), DELM, LED(1, 1), DELM, LED(2, 1), DELM, LED(3, 1),
	DELM, LED(4, 1), DELM, LED(5, 1), DELM, LED(6, 1), DELM, LED(7, 1),
	DELM, LED(0, 2), DELM, LED(1, 2), DELM, LED(2, 2), DELM, LED(3, 2),
	DELM, LED(4, 2), DELM, LED(5, 2), DELM, LED(6, 2), DELM, LED(7, 2),
	DELM, LED(0, 3), DELM, LED(1, 3), DELM, LED(2, 3), DELM, LED(3, 3),
	DELM, LED(4, 3), DELM, LED(5, 3), DELM, LED(6, 3), DELM, LED(7, 3),
	DELM, LED(0, 4), DELM, LED(1, 4), DELM, LED(2, 4), DELM, LED(3, 4),
	DELM, LED(4, 4), DELM, LED(5, 4), DELM, LED(6, 4), DELM, LED(7, 4),
	DELM, DELM,
};
#undef LED
#undef DELM
const size_t rgbkbd_map_size = ARRAY_SIZE(rgbkbd_map);

BUILD_ASSERT(IS_ENABLED(CONFIG_KEYBOARD_VIVALDI));
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &taniks_kb;
}
