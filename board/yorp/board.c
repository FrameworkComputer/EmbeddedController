/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yorp board-specific configuration */

#include "common.h"
#include "gpio.h"
#include "lid_switch.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

/* TODO(b/73811887): Fill out correctly */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);
