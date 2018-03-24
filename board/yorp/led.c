/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Yorp
 */

#include "gpio.h"
#include "hooks.h"

static void led_init(void)
{
	/*
	 * Temporary hack to turn on blue led to indicate that EC is up and
	 * running. This can be removed after adding proper LED support
	 * (b/74952719).
	 */
	gpio_set_level(GPIO_BAT_LED_BLUE_L, 0);
	gpio_set_level(GPIO_BAT_LED_ORANGE_L, 1);
}

DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);
