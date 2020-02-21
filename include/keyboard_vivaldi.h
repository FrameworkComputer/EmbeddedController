/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __KEYBOARD_VIVALDI_H__
#define __KEYBOARD_VIVALDI_H__

#include <keyboard_8042_sharedlib.h>

enum vivaldi_top_keys {
	T1 = 0,
	T2,
	T3,
	T4,
	T5,
	T6,
	T7,
	T8,
	T9,
	T10,
	T11,
	T12,
	T13,
	T14,
	T15,
	MAX_VIVALDI_KEYS
};

struct vivaldi_config {
	uint8_t num_top_row_keys;
	uint16_t scancodes[MAX_VIVALDI_KEYS];
};

void vivaldi_init(const struct vivaldi_config *vivaldi_config);

#endif /* __KEYBOARD_VIVALDI_H__ */
