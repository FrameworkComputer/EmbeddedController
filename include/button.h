/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Button API for Chrome EC */

#ifndef __CROS_EC_BUTTON_H
#define __CROS_EC_BUTTON_H

#include "common.h"
#include "gpio.h"

#define BUTTON_FLAG_ACTIVE_HIGH (1 << 0)

enum keyboard_button_type {
	KEYBOARD_BUTTON_POWER = 0,
	KEYBOARD_BUTTON_VOLUME_DOWN,
	KEYBOARD_BUTTON_VOLUME_UP,

	KEYBOARD_BUTTON_COUNT
};

#endif  /* __CROS_EC_BUTTON_H */
