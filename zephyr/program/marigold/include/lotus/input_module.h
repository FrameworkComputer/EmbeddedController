/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_INPUT_MODULE_H__
#define __BOARD_INPUT_MODULE_H__

enum input_modules_t {
	INPUT_MODULE_SHORT,
	INPUT_MODULE_RESERVED_1,
	INPUT_MODULE_RESERVED_2,
	INPUT_MODULE_RESERVED_3,
	INPUT_MODULE_RESERVED_4,
	INPUT_MODULE_RESERVED_5,
	INPUT_MODULE_RESERVED_6,
	INPUT_MODULE_RESERVED_7,
	INPUT_MODULE_GENERIC_A,
	INPUT_MODULE_GENERIC_B,
	INPUT_MODULE_GENERIC_C,
	INPUT_MODULE_KEYBOARD_B,
	INPUT_MODULE_KEYBOARD_A,
	INPUT_MODULE_TOUCHPAD,
	INPUT_MODULE_RESERVED_14,
	INPUT_MODULE_DISCONNECTED,
};

enum input_deck_state {
	DECK_OFF,
	DECK_DISCONNECTED,
	DECK_TURNING_ON,
	DECK_ON,
	DECK_FORCE_OFF,
	DECK_FORCE_ON,
	DECK_NO_DETECTION /* input deck will follow power sequence, no present check */
};

enum input_deck_mux {
	TOP_ROW_0 = 0,
	TOP_ROW_1,
	TOP_ROW_2,
	TOP_ROW_3,
	TOP_ROW_4,
	TOUCHPAD,
	TOP_ROW_NOT_CONNECTED,
	HUBBOARD = 7
};

int get_deck_state(void);

bool input_deck_is_fully_populated(void);

void input_modules_powerdown(void);

void set_detect_mode(int mode);

int get_detect_mode(void);

#endif /*__BOARD_INPUT_MODULE_H__*/
