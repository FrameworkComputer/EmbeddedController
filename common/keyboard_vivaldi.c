/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Vivali Keyboard code for Chrome EC */

#include "keyboard_8042_sharedlib.h"
#include "keyboard_scan.h"
#include "keyboard_vivaldi.h"

/*
 * Row Column info for Top row keys T1 - T15
 * Ref: https://drive.google.com/corp/drive/folders/17UtVQ-AixnlQuicRPTp8t46HE-sT522E
 */
static const struct key {
	uint8_t row;
	uint8_t col;
} vivaldi[MAX_VIVALDI_KEYS] = {
	[T1] = {.row = 0, .col = 2},
	[T2] = {.row = 3, .col = 2},
	[T3] = {.row = 2, .col = 2},
	[T4] = {.row = 1, .col = 2},
	[T5] = {.row = 3, .col = 4},
	[T6] = {.row = 2, .col = 4},
	[T7] = {.row = 1, .col = 4},
	[T8] = {.row = 2, .col = 9},
	[T9] = {.row = 1, .col = 9},
	[T10] = {.row = 0, .col = 4},
	[T11] = {.row = 0, .col = 1},
	[T12] = {.row = 1, .col = 5},
	[T13] = {.row = 3, .col = 5},
	[T14] = {.row = 0, .col = 9},
	[T15] = {.row = 0, .col = 11},
};

void vivaldi_init(const struct vivaldi_config *keybd)
{
	uint8_t row, col, *mask;
	int key;

	cprints(CC_KEYBOARD, "VIVALDI: Num top row keys = %u\n",
		keybd->num_top_row_keys);

	if (keybd->num_top_row_keys > MAX_VIVALDI_KEYS ||
	    keybd->num_top_row_keys < 10)
		cprints(CC_KEYBOARD,
			"BAD VIVALDI CONFIG! Some keys may not work\n");

	for (key = T1; key < MAX_VIVALDI_KEYS; key++) {

		row = vivaldi[key].row;
		col = vivaldi[key].col;
		mask = keyscan_config.actual_key_mask + col;

		if (key < keybd->num_top_row_keys && keybd->scancodes[key]) {

			/* Enable the mask */
			*mask |= (1 << row);

			/* Populate the scancode */
			scancode_set2[col][row] = keybd->scancodes[key];
		} else {
			/* Disable the mask */
			*mask &= ~(1 << row);
		}
	}
}
