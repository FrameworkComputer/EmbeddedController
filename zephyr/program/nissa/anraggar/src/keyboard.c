/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
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
	/* New keyboard maxtrix mapped */
	.actual_key_mask = {
		0x48, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xa4, 0xff, 0xff, 0x55, 0xfb, 0xca, 0x40, 0x0a,
	},
};

test_export_static const struct ec_response_keybd_config anraggar_kb = {
	.num_top_row_keys = 15,
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
		TK_MICMUTE,		/* T11 */
		TK_MENU,		/* T12 */
		TK_PREV_TRACK,		/* T13 */
		TK_PLAY_PAUSE,		/* T14 */
		TK_NEXT_TRACK,		/* T15 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &anraggar_kb;
}
