/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_INPUT_MODULE_H__
#define __BOARD_INPUT_MODULE_H__

enum input_deck_state {
	DECK_OFF,
	DECK_DISCONNECTED,
	DECK_TURNING_ON,
	DECK_ON,
	DECK_FORCE_OFF,
	DECK_FORCE_ON,
	DECK_NO_DETECTION /* input deck will follow power sequence, no present check */
};

void input_c_deck_powerdown(void);

#endif /*__BOARD_INPUT_MODULE_H__*/
