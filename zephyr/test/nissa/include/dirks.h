/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_NISSA_INCLUDE_DIRKS_H_
#define ZEPHYR_TEST_NISSA_INCLUDE_DIRKS_H_

#include "ec_commands.h"

enum led_color {
	LED_OFF = 0,
	LED_BLUE,
	LED_RED,
	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

enum charge_port {
	CHARGE_PORT_TYPEC0,
	CHARGE_PORT_BARRELJACK,
};

void board_bj_init(void);
bool board_is_power_good(void);

#endif /* ZEPHYR_TEST_NISSA_INCLUDE_DIRKS_H_ */
