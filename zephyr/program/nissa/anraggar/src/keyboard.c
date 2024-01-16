/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "keyboard_scan.h"
#include "timer.h"

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