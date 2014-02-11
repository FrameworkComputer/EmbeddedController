/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wireless API for Chrome EC */

#ifndef __CROS_EC_WIRELESS_H
#define __CROS_EC_WIRELESS_H

#include "common.h"

/* Wireless power state for wireless_set_state() */
enum wireless_power_state {
	WIRELESS_OFF,
	WIRELESS_SUSPEND,
	WIRELESS_ON
};

/**
 * Set wireless power state.
 */
void wireless_set_state(enum wireless_power_state state);

#endif  /* __CROS_EC_WIRELESS_H */
