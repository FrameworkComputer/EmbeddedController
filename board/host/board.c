/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Emulator board-specific configuration */

#include "board.h"
#include "gpio.h"

const struct gpio_info gpio_list[GPIO_COUNT] = {
	{"EC_INT", 0, 0, 0, 0},
	{"LID_OPEN", 0, 0, 0, 0},
	{"POWER_BUTTON_L", 0, 0, 0, 0},
};
