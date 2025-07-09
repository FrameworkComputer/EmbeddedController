/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "keyboard_scan.h"
#include "timer.h"

static const struct ec_response_keybd_config keybd1 = {
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

/* TK_REFRESH is always T2 above, vivaldi_keys are not overridden. */
BUILD_ASSERT_REFRESH_RC(3, 2);

BUILD_ASSERT(IS_ENABLED(CONFIG_KEYBOARD_VIVALDI));
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &keybd1;
}

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 }, { 1, 1 },	  { 1, 0 },   { 0, 6 },
	{ 0, 7 },   { 1, 4 }, { 1, 3 },	  { 1, 6 },   { 1, 7 },
	{ 3, 1 },   { 2, 0 }, { 1, 5 },	  { 2, 6 },   { 2, 7 },
	{ 2, 1 },   { 2, 4 }, { 2, 5 },	  { 1, 2 },   { 2, 3 },
	{ 2, 2 },   { 3, 0 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
#endif
