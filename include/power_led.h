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

/* Set the power adapter LED to the specified color. */
int powerled_set(enum powerled_color color);

#endif  /* __CROS_EC_POWER_LED_H */
