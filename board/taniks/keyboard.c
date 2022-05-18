/* Copyright 2021 The Chromium OS Authors. All rights reserved.
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
		0xa4, 0xff, 0xff, 0x55, 0xff, 0xff, 0xff, 0xff,  /* full set */
	},
	.ksi_threshold_mv = 250,
};

static const struct ec_response_keybd_config taniks_kb = {
	.num_top_row_keys = 14,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_ABSENT,		/* T8 */
		TK_ABSENT,		/* T9 */
		TK_ABSENT,		/* T10 */
		TK_MICMUTE,		/* T11 */
		TK_VOL_MUTE,		/* T12 */
		TK_VOL_DOWN,		/* T13 */
		TK_VOL_UP,		/* T14 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY | KEYBD_CAP_NUMERIC_KEYPAD,
};

static struct rgb_s grid0[RGB_GRID0_COL * RGB_GRID0_ROW];

struct rgbkbd rgbkbds[] = {
	[0] = {
		.cfg = &(const struct rgbkbd_cfg) {
			.drv = &aw20198_drv,
			.i2c = I2C_PORT_KBMCU,
			.col_len = RGB_GRID0_COL,
			.row_len = RGB_GRID0_ROW,
		},
		.init = &rgbkbd_default,
		.buf = grid0,
	},
};
const uint8_t rgbkbd_count = ARRAY_SIZE(rgbkbds);

const uint8_t rgbkbd_hsize = RGB_GRID0_COL;
const uint8_t rgbkbd_vsize = RGB_GRID0_ROW;

#define LED(x, y)	RGBKBD_COORD((x), (y))
#define DELM		RGBKBD_DELM
const uint8_t rgbkbd_map[] = {
	DELM,				/* 0: (null) */
	LED( 0, 0), DELM,		/* 1: ~ ` */
	LED( 2, 0), LED( 4, 0), DELM,	/* 2: ! 1 */
	LED( 6, 0), DELM,		/* 3: @ 2 */
	LED( 0, 1), DELM,		/* 4: # 3 */
	LED( 2, 1), DELM,		/* 5: $ 4 */
	LED( 4, 1), LED( 6, 1), DELM,	/* 6: % 5 */
	LED( 0, 2), DELM,		/* 7: ^ 6 */
	LED( 2, 2), DELM,		/* 8: & 7 */
	LED( 4, 2), DELM,		/* 9: * 8 */
	LED( 6, 2), DELM,		/* 10: ( 9 */
	LED( 0, 3), DELM,		/* 11: ) 0 */
	LED( 1, 3), DELM,		/* 12: _ - */
	LED( 3, 3), DELM,		/* 13: + = */
	DELM,				/* 14: (null) */
	LED( 5, 3), LED( 6, 3), DELM,	/* 15: backspace */
	LED( 0, 0), DELM,		/* 16: tab */
	LED( 2, 0), LED( 4, 0), DELM,	/* 17: q */
	LED( 6, 0), DELM,		/* 18: w */
	LED( 0, 1), DELM,		/* 19: e */
	LED( 2, 1), DELM,		/* 20: r */
	LED( 4, 1), LED( 6, 1), DELM,	/* 21: t */
	LED( 0, 2), DELM,		/* 22: y */
	LED( 2, 2), DELM,		/* 23: u */
	LED( 4, 2), DELM,		/* 24: i */
	LED( 6, 2), DELM,		/* 25: o */
	LED( 0, 3), LED( 1, 3), DELM,	/* 26: p */
	LED( 3, 3), DELM,		/* 27: [ { */
	LED( 5, 3), DELM,		/* 28: ] } */
	LED( 6, 3), DELM,		/* 29: \ | */
	LED( 0, 0), DELM,		/* 30: caps lock */
	LED( 2, 0), LED( 4, 0), DELM,	/* 31: a */
	LED( 6, 0), DELM,		/* 32: s */
	LED( 0, 1), DELM,		/* 33: d */
	LED( 2, 1), DELM,		/* 34: f */
	LED( 4, 1), LED( 6, 1), DELM,	/* 35: g */
	LED( 0, 2), DELM,		/* 36: h */
	LED( 2, 2), DELM,		/* 37: j */
	LED( 4, 2), DELM,		/* 38: k */
	LED( 6, 4), DELM,		/* 39: l */
	LED( 0, 3), LED( 1, 3), DELM,	/* 40: ; : */
	LED( 3, 3), DELM,		/* 41: " ' */
	DELM,				/* 42: (null) */
	LED( 5, 3), LED( 6, 3), DELM,	/* 43: enter */
	LED( 1, 0), LED( 3, 0), DELM,	/* 44: L-shift */
	DELM,				/* 45: (null) */
	LED( 5, 0), DELM,		/* 46: z */
	LED( 7, 0), DELM,		/* 47: x */
	LED( 1, 1), DELM,		/* 48: c */
	LED( 3, 1), DELM,		/* 49: v */
	LED( 5, 1), LED( 7, 1), DELM,	/* 50: b */
	LED( 1, 2), DELM,		/* 51: n */
	LED( 3, 2), DELM,		/* 52: m */
	LED( 5, 2), DELM,		/* 53: , < */
	LED( 7, 2), DELM,		/* 54: . > */
	LED( 2, 3), DELM,		/* 55: / ? */
	DELM,				/* 56: (null) */
	LED( 4, 3), LED( 7, 3), DELM,	/* 57: R-shift */
	LED( 1, 0), LED( 3, 0), DELM,	/* 58: L-ctrl */
	LED( 5, 3), LED( 6, 3), DELM,	/* 59: power */
	LED( 5, 0), LED( 7, 0), DELM,	/* 60: L-alt */
	LED( 1, 1), LED( 3, 1),
	LED( 5, 1), LED( 7, 1),
	LED( 1, 2), LED( 3, 2), DELM,	/* 61: space */
	LED( 5, 2), DELM,		/* 62: R-alt */
	DELM,				/* 63: (null) */
	LED( 7, 2), DELM,		/* 64: R-ctrl */
	DELM,				/* 65: (null) */
	DELM,				/* 66: (null) */
	DELM,				/* 67: (null) */
	DELM,				/* 68: (null) */
	DELM,				/* 69: (null) */
	DELM,				/* 70: (null) */
	DELM,				/* 71: (null) */
	DELM,				/* 72: (null) */
	DELM,				/* 73: (null) */
	DELM,				/* 74: (null) */
	DELM,				/* 75: (null) */
	LED( 0, 4), DELM,		/* 76: delete */
	DELM,				/* 77: (null) */
	DELM,				/* 78: (null) */
	LED( 2, 3), DELM,		/* 79: left */
	LED( 4, 4), DELM,		/* 80: home */
	LED( 6, 4), DELM,		/* 81: end */
	DELM,				/* 82: (null) */
	LED( 4, 3), DELM,		/* 83: up */
	LED( 4, 3), DELM,		/* 84: down */
	LED( 0, 4), DELM,		/* 85: page up */
	LED( 2, 4), DELM,		/* 86: page down */
	DELM,				/* 87: (null) */
	DELM,				/* 88: (null) */
	LED( 7, 3), DELM,		/* 89: right */
	DELM,				/* 90: (null) */
	LED( 0, 4), DELM,		/* 91: numpad 7 */
	LED( 0, 4), DELM,		/* 92: numpad 4 */
	LED( 1, 4), DELM,		/* 93: numpad 1 */
	DELM,				/* 94: (null) */
	LED( 2, 4), DELM,		/* 95: numpad / */
	LED( 2, 4), DELM,		/* 96: numpad 8 */
	LED( 2, 4), DELM,		/* 97: numpad 5 */
	LED( 3, 4), DELM,		/* 98: numpad 2 */
	LED( 3, 4), DELM,		/* 99: numpad 0 */
	LED( 4, 4), DELM,		/* 100: numpad * */
	LED( 4, 4), DELM,		/* 101: numpad 9 */
	LED( 4, 4), DELM,		/* 102: numpad 6 */
	LED( 5, 4), DELM,		/* 103: numpad 3 */
	LED( 5, 4), DELM,		/* 104: numpad . */
	LED( 6, 4), DELM,		/* 105: numpad - */
	LED( 6, 4), DELM,		/* 106: numpad + */
	DELM,				/* 107: (null) */
	LED( 7, 4), DELM,		/* 108: numpad enter */
	DELM,				/* 109: (null) */
	LED( 0, 0), DELM,		/* 110: esc */
	LED( 2, 0), LED( 4, 0), DELM,	/* T1: back */
	LED( 6, 0), DELM,		/* T2: refresh */
	LED( 0, 1), DELM,		/* T3: full screen */
	LED( 2, 1), DELM,		/* T4: overview */
	LED( 4, 1), LED( 6, 1), DELM,	/* T5: snapshot */
	LED( 0, 2), DELM,		/* T6: brightness down */
	LED( 2, 2), DELM,		/* T7: brightness up */
	DELM,				/* T8: (null) */
	DELM,				/* T9: (null) */
	DELM,				/* T10: (null) */
	LED( 4, 2), DELM,		/* T11: mic mute */
	LED( 6, 2), DELM,		/* T12: volume mute */
	LED( 0, 3), LED( 1, 3), DELM,	/* T13: volume down */
	LED( 3, 3), DELM,		/* T14: volume up */
	DELM,				/* T15: (null) */
	DELM,				/* 126: (null) */
	DELM,				/* 127: (null) */
};
#undef LED
#undef DELM
const size_t rgbkbd_map_size = ARRAY_SIZE(rgbkbd_map);

__override const struct ec_response_keybd_config
*board_vivaldi_keybd_config(void)
{
	return &taniks_kb;
}
