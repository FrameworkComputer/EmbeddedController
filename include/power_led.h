/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power LED control for Chrome EC */

#ifndef __CROS_EC_POWER_LED_H
#define __CROS_EC_POWER_LED_H

#include "common.h"

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
