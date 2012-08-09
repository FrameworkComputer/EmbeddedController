/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power LED control for Chrome EC */

#ifndef __CROS_EC_POWER_LED_H
#define __CROS_EC_POWER_LED_H

#include "common.h"

enum powerled_color {
	POWERLED_OFF = 0,
	POWERLED_RED,
	POWERLED_YELLOW,
	POWERLED_GREEN,
	POWERLED_COLOR_COUNT  /* Number of colors, not a color itself */
};

enum powerled_state {
	POWERLED_STATE_OFF,
	POWERLED_STATE_ON,
	POWERLED_STATE_SUSPEND,
	POWERLED_STATE_COUNT
};

enum powerled_config {
	POWERLED_CONFIG_MANUAL_OFF,
	POWERLED_CONFIG_MANUAL_ON,
	POWERLED_CONFIG_PWM,
};

/* Set the power adapter LED to the specified color. */
int powerled_set(enum powerled_color color);

/* Set the power LED according to the specified state. */
void powerled_set_state(enum powerled_state state);

#endif  /* __CROS_EC_POWER_LED_H */
