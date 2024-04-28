/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"

test_export_static const struct ec_response_keybd_config spikyrock_keyboard = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,
		TK_REFRESH,
		TK_FULLSCREEN,
		TK_OVERVIEW,
		TK_SNAPSHOT,
		TK_BRIGHTNESS_DOWN,
		TK_BRIGHTNESS_UP,
		TK_VOL_MUTE,
		TK_VOL_DOWN,
		TK_VOL_UP,
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &spikyrock_keyboard;
}
