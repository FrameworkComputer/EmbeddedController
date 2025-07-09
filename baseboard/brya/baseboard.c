/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "gpio_signal.h"
#include "keyboard_scan.h"
#include "tablet_mode.h"

/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_ACOK_OD,
	GPIO_GSC_EC_PWR_BTN_ODL,
	GPIO_LID_OPEN,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);
