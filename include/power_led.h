/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power LED control for Chrome EC */

#ifndef __CROS_EC_POWER_LED_H
#define __CROS_EC_POWER_LED_H

#include "common.h"

/* Interface for LM4-based boards */

enum powerled_color {
	POWERLED_OFF = 0,
	POWERLED_RED,
	POWERLED_YELLOW,
	POWERLED_GREEN,
	POWERLED_COLOR_COUNT  /* Number of colors, not a color itself */
};

#ifdef CONFIG_POWER_LED

/**
 * Set the power adapter LED
 *
 * @param color		Color to set LED
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int powerled_set(enum powerled_color color);

#else

static inline int powerled_set(enum powerled_color color) { return 0; }

#endif

/* Interface for STM32-based boards */

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

#ifdef CONFIG_TASK_POWERLED

/**
 * Set the power LED
 *
 * @param state		Target state
 */
void powerled_set_state(enum powerled_state state);

#else

static inline void powerled_set_state(enum powerled_state state) {}

#endif

#endif /* __CROS_EC_POWER_LED_H */
