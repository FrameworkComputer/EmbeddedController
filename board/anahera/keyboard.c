/* Copyright 2021 The Chromium OS Authors. All rights reserved.
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
	/* Increase from 50 us, because KSO_02 passes through the H1. */
	.output_settle_us = 80,
	/* Other values should be the same as the default configuration. */
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x1c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

static const struct ec_response_keybd_config keybd_wo_privacy_w_kblight = {
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

static const struct ec_response_keybd_config keybd_wo_privacy_wo_kblight = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,			/* T1 */
		TK_REFRESH,			/* T2 */
		TK_FULLSCREEN,			/* T3 */
		TK_OVERVIEW,			/* T4 */
		TK_SNAPSHOT,			/* T5 */
		TK_BRIGHTNESS_DOWN,		/* T6 */
		TK_BRIGHTNESS_UP,		/* T7 */
		TK_PREV_TRACK,			/* T8 */
		TK_PLAY_PAUSE,			/* T9 */
		TK_MICMUTE,			/* T10 */
		TK_VOL_MUTE,			/* T11 */
		TK_VOL_DOWN,			/* T12 */
		TK_VOL_UP,			/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

static const struct ec_response_keybd_config keybd_w_privacy_w_kblight = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_PRIVACY_SCRN_TOGGLE,	/* T8 */
		TK_KBD_BKLIGHT_TOGGLE,	/* T9 */
		TK_MICMUTE,		/* T10 */
		TK_VOL_MUTE,		/* T11 */
		TK_VOL_DOWN,		/* T12 */
		TK_VOL_UP,		/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

static const struct ec_response_keybd_config keybd_w_privacy_wo_kblight = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,			/* T1 */
		TK_REFRESH,			/* T2 */
		TK_FULLSCREEN,			/* T3 */
		TK_OVERVIEW,			/* T4 */
		TK_SNAPSHOT,			/* T5 */
		TK_BRIGHTNESS_DOWN,		/* T6 */
		TK_BRIGHTNESS_UP,		/* T7 */
		TK_PRIVACY_SCRN_TOGGLE,		/* T8 */
		TK_PLAY_PAUSE,			/* T9 */
		TK_MICMUTE,			/* T10 */
		TK_VOL_MUTE,			/* T11 */
		TK_VOL_DOWN,			/* T12 */
		TK_VOL_UP,			/* T13 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	if (ec_cfg_has_eps()) {
		if (ec_cfg_has_kblight())
			return &keybd_w_privacy_w_kblight;
		else
			return &keybd_w_privacy_wo_kblight;
	} else {
		if (ec_cfg_has_kblight())
			return &keybd_wo_privacy_w_kblight;
		else
			return &keybd_wo_privacy_wo_kblight;
	}
}

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
		{-1, -1}, {0, 5}, {1, 1}, {1, 0}, {0, 6},
		{0, 7}, {1, 4}, {1, 3}, {1, 6}, {1, 7},
		{3, 1}, {2, 0}, {1, 5}, {2, 6}, {2, 7},
		{2, 1}, {2, 4}, {2, 5}, {1, 2}, {2, 3},
		{2, 2}, {3, 0}, {-1, -1}, {-1, -1}, {-1, -1},
};

const int keyboard_factory_scan_pins_used =
			ARRAY_SIZE(keyboard_factory_scan_pins);
#endif
