/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_scan.h"
#include "rgb_keyboard.h"
#include "timer.h"

#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ##args)

const struct key {
	uint8_t row;
	uint8_t col;
} vivaldi_keys[] = {
	{ .row = 4, .col = 2 }, /* T1 */
	{ .row = 3, .col = 2 }, /* T2 */
	{ .row = 2, .col = 2 }, /* T3 */
	{ .row = 1, .col = 2 }, /* T4 */
	{ .row = 4, .col = 4 }, /* T5 */
	{ .row = 3, .col = 4 }, /* T6 */
	{ .row = 2, .col = 4 }, /* T7 */
	{ .row = 2, .col = 9 }, /* T8 */
	{ .row = 1, .col = 9 }, /* T9 */
	{ .row = 1, .col = 4 }, /* T10 */
	{ .row = 0, .col = 4 }, /* T11 */
	{ .row = 1, .col = 5 }, /* T12 */
	{ .row = 3, .col = 5 }, /* T13 */
	{ .row = 2, .col = 1 }, /* T14 */
	{ .row = 0, .col = 1 }, /* T15 */
};
BUILD_ASSERT(ARRAY_SIZE(vivaldi_keys) == MAX_TOP_ROW_KEYS);

static const struct ec_response_keybd_config osiris_vivaldi_kb = {
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

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &osiris_vivaldi_kb;
}

extern struct rgbkbd_drv is31fl3733b_drv;

static struct rgb_s grid0[RGB_GRID0_COL * RGB_GRID0_ROW];

struct rgbkbd rgbkbds[] = {
	[0] = {
		.cfg = &(const struct rgbkbd_cfg) {
			.drv = &is31fl3733b_drv,
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

enum ec_rgbkbd_type rgbkbd_type = EC_RGBKBD_TYPE_FOUR_ZONES_12_LEDS;

#define LED(x, y) RGBKBD_COORD((x), (y))
#define DELM RGBKBD_DELM

const uint8_t rgbkbd_map[] = {
	DELM, LED(0, 0), DELM, LED(1, 0), DELM, LED(2, 0),  DELM, LED(3, 0),
	DELM, LED(4, 0), DELM, LED(5, 0), DELM, LED(6, 0),  DELM, LED(7, 0),
	DELM, LED(8, 0), DELM, LED(9, 0), DELM, LED(10, 0), DELM, LED(11, 0),
	DELM, LED(0, 1), DELM, LED(1, 1), DELM, LED(2, 1),  DELM, LED(3, 1),
	DELM, LED(4, 1), DELM, LED(5, 1), DELM, LED(6, 1),  DELM, LED(7, 1),
	DELM, LED(8, 1), DELM, LED(9, 1), DELM, LED(10, 1), DELM, LED(11, 1),
	DELM, DELM,
};
#undef LED
#undef DELM
const size_t rgbkbd_map_size = ARRAY_SIZE(rgbkbd_map);

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
		0x86, 0xff, 0xff, 0x55, 0xff, 0xff, 0xff,
		0xff  /* full set */
	},
};

static scancode_set2_t scancode_set2_rgb = {
	{ 0x0000, 0x0000, 0x0014, 0xe01f, 0xe014, 0xe007, 0x0000, 0x0000 },
	{ 0x001f, 0x0076, 0x0017, 0x000e, 0x001c, 0x003a, 0x000d, 0x0016 },
	{ 0x006c, 0x000c, 0x0004, 0x0006, 0x0005, 0xe071, 0x0026, 0x002a },
	{ 0x0032, 0x0034, 0x002c, 0x002e, 0x002b, 0x0029, 0x0025, 0x002d },
	{ 0x0078, 0x0009, 0x0083, 0x000b, 0x0003, 0x0041, 0x001e, 0x001d },
	{ 0x0051, 0x0007, 0x005b, 0x0000, 0x0042, 0x0022, 0x003e, 0x0043 },
	{ 0x0031, 0x0033, 0x0035, 0x0036, 0x003b, 0x001b, 0x003d, 0x003c },
	{ 0x0000, 0x0012, 0x0061, 0x0000, 0x0000, 0x0000, 0x0000, 0x0059 },
	{ 0x0055, 0x0052, 0x0054, 0x004e, 0x004c, 0x0024, 0x0044, 0x004d },
	{ 0x0045, 0x0001, 0x000a, 0x002f, 0x004b, 0x0049, 0x0046, 0x001A },
	{ 0xe011, 0x0000, 0x006a, 0x0000, 0x005d, 0x0000, 0x0011, 0x0000 },
	{ 0xe07a, 0x005d, 0xe075, 0x006b, 0x005a, 0xe072, 0x004a, 0x0066 },
	{ 0xe06b, 0xe074, 0xe069, 0x0067, 0xe0c6, 0x0064, 0x0015, 0xe07d },
	{ 0x0073, 0x0066, 0xe071, 0x005d, 0x005a, 0xe04a, 0x0070, 0x0021 },
	{ 0x0023, 0xe05a, 0x0075, 0x0067, 0xe069, 0xe07a, 0x007d, 0x0069 },
};
BUILD_ASSERT(ARRAY_SIZE(scancode_set2_rgb) == KEYBOARD_COLS_MAX);

static void keyboard_matrix_init(void)
{
	CPRINTS("%s", __func__);

	register_scancode_set2(&scancode_set2_rgb,
			       ARRAY_SIZE(scancode_set2_rgb));
}
DECLARE_HOOK(HOOK_INIT, keyboard_matrix_init, HOOK_PRIO_PRE_DEFAULT);
