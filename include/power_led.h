/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button LED control for Chrome EC */

#ifndef __CROS_EC_POWER_LED_H
#define __CROS_EC_POWER_LED_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum powerled_state {
	POWERLED_STATE_OFF,
	POWERLED_STATE_ON,
	POWERLED_STATE_SUSPEND,
	POWERLED_STATE_COUNT
};

#ifdef HAS_TASK_POWERLED

/**
 * Set the power LED
 *
 * @param state		Target state
 */
void powerled_set_state(enum powerled_state state);

#else

static inline void powerled_set_state(enum powerled_state state)
{
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_POWER_LED_H */
